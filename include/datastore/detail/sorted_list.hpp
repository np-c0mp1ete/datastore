#pragma once

#include <atomic>
#include <memory>
#include <mutex>

namespace datastore::detail
{
// Implementation is based on the fine-grained locking list implementation
// from Chapter 6 of "C++ Concurrency in Action" by A. Williams
template <typename T, typename Compare = std::less<T>>
class sorted_list
{
    struct node
    {
        node()
            : next()
        {
        }

        node(T const& value)
            : data(std::make_shared<T>(value))
        {
        }

        node(node const& other) = delete;
        node(node&& other) noexcept = default;

        node& operator=(node const& other) = delete;
        node& operator=(node&& other) noexcept = default;

        std::mutex m;
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;
    };

  public:
    sorted_list(const Compare& comp = Compare())
        : comp_(comp)
    {
    }

    sorted_list(sorted_list const& other) = delete;

    sorted_list(sorted_list&& other) noexcept
        : head_(std::move(other.head_)),
          comp_(std::move(other.comp_)),
          num_elements_(other.num_elements_.load())
    {
    }

    sorted_list& operator=(sorted_list const& other) = delete;

    sorted_list& operator=(sorted_list&& other) noexcept
    {
        head_ = std::move(other.head_);
        comp_ = std::move(other.comp_);
        num_elements_ = other.num_elements_.load();

        return *this;
    }

    void push(T const& value)
    {
        std::unique_ptr<node> new_node(new node(value));

        node* current = head_.get();
        std::unique_lock<std::mutex> lk(head_->m);
        while (node* const next = current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m);
            if (comp_(*new_node->data, *next->data))
            {
                break;
            }
            lk.unlock();
            current = next;
            lk = std::move(next_lk);
        }

        new_node->next = std::move(current->next);
        current->next = std::move(new_node);

        ++num_elements_;
    }

    std::shared_ptr<T> front()
    {
        node* current = head_.get();
        std::unique_lock<std::mutex> lk(head_->m);
        while (node* const next = current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m);
            lk.unlock();
            return next->data;
        }
        return std::shared_ptr<T>();
    }

    size_t size()
    {
        return num_elements_;
    }

    template <typename Function>
    void for_each(Function f) const
    {
        node* current = head_.get();
        std::unique_lock<std::mutex> lk(head_->m);
        while (node* const next = current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m);
            lk.unlock();
            f(*next->data);
            current = next;
            lk = std::move(next_lk);
        }
    }

    template <typename Predicate>
    std::shared_ptr<T> find_first_if(Predicate p) const
    {
        node* current = head_.get();
        std::unique_lock<std::mutex> lk(head_->m);
        while (node* const next = current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m);
            lk.unlock();
            if (p(*next->data))
            {
                return next->data;
            }
            current = next;
            lk = std::move(next_lk);
        }
        return std::shared_ptr<T>();
    }

    template <typename Predicate>
    void remove_if(Predicate p)
    {
        node* current = head_.get();
        std::unique_lock<std::mutex> lk(head_->m);
        while (node* const next = current->next.get())
        {
            std::unique_lock<std::mutex> next_lk(next->m);
            if (p(*next->data))
            {
                std::unique_ptr<node> old_next = std::move(current->next);
                current->next = std::move(next->next);
                next_lk.unlock();
                --num_elements_;
            }
            else
            {
                lk.unlock();
                current = next;
                lk = std::move(next_lk);
            }
        }
    }

  private:
    std::unique_ptr<node> head_ = std::make_unique<node>();
    Compare comp_;
    std::atomic_size_t num_elements_ = 0;
};
} // namespace datastore::detail
