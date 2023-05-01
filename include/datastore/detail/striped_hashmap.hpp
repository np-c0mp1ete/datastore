#pragma once

#include <atomic>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace datastore::detail
{
template <typename Key, typename Value>
class striped_hashmap
{
  private:
    class bucket_type
    {
      private:
        friend class striped_hashmap;

        using bucket_value = std::pair<Key, Value>;
        using bucket_data = std::list<bucket_value>;

        bucket_data data;
        mutable std::shared_mutex mutex;

        auto find_entry_for(Key const& key)
        {
            return std::find_if(data.begin(), data.end(), [&](bucket_value const& item) {
                return item.first == key;
            });
        }

        auto find_entry_for(Key const& key) const
        {
            return std::find_if(data.begin(), data.end(), [&](bucket_value const& item) {
                return item.first == key;
            });
        }

      public:
        std::optional<Value> value_for(Key const& key) const
        {
            std::shared_lock lock(mutex);
            auto found_entry = find_entry_for(key);
            return (found_entry == data.end()) ? std::nullopt : std::make_optional<Value>(found_entry->second);
        }

        template <typename K, typename V>
        std::pair<Value, bool> find_or_insert_with_limit(K&& key, V&& value, std::atomic_size_t& cur_size,
                                                         size_t max_size)
        {
            std::unique_lock lock(mutex);
            auto found_entry = find_entry_for(std::forward<K>(key));
            if (found_entry == data.end())
            {
                size_t expected = cur_size.load(std::memory_order_relaxed);
                if (expected >= max_size ||
                    !cur_size.compare_exchange_weak(expected, expected + 1, std::memory_order_relaxed))
                    return std::make_pair<Value, bool>(Value(), false);

                data.push_back(bucket_value(std::forward<K>(key), std::forward<V>(value)));
            }

            auto entry = find_entry_for(std::forward<K>(key));
            return std::pair<Value, bool>(entry->second, true);
        }

        template <typename K, typename V>
        bool insert_with_limit_or_assign(K&& key, V&& value, std::atomic_size_t& cur_size, size_t max_size)
        {
            std::unique_lock lock(mutex);
            auto found_entry = find_entry_for(std::forward<K>(key));
            if (found_entry == data.end())
            {
                size_t expected = cur_size.load(std::memory_order_relaxed);
                if (expected >= max_size ||
                    !cur_size.compare_exchange_weak(expected, expected + 1, std::memory_order_relaxed))
                    return false;

                data.push_back(bucket_value(std::forward<K>(key), std::forward<V>(value)));
            }
            else
            {
                found_entry->second = std::forward<V>(value);
            }
            return true;
        }

        size_t remove_mapping(Key const& key)
        {
            std::unique_lock lock(mutex);
            auto found_entry = find_entry_for(key);
            if (found_entry != data.end())
            {
                data.erase(found_entry);
                return 1;
            }
            return 0;
        }
    };

  public:
    using key_type = Key;
    using mapped_type = Value;

    striped_hashmap(unsigned num_buckets = 13)
        : buckets_(num_buckets)
    {
        for (unsigned i = 0; i < num_buckets; ++i)
        {
            buckets_[i].reset(new bucket_type);
        }
    }

    striped_hashmap(striped_hashmap const& other) = delete;

    striped_hashmap(striped_hashmap&& other) noexcept
        : buckets_(std::move(other.buckets_)),
          num_elements_(other.num_elements_.load())
    {
    }

    striped_hashmap& operator=(striped_hashmap const& other) = delete;

    striped_hashmap& operator=(striped_hashmap&& other) noexcept
    {
        buckets_ = std::move(other.buckets_);
        num_elements_ = other.num_elements_.load();

        return *this;
    }

    [[nodiscard]] std::optional<Value> find(Key const& key) const
    {
        return bucket(key).value_for(key);
    }

    template <typename K, typename V>
    bool insert_with_limit_or_assign(K&& key, V&& value, size_t max_num_elements)
    {
        return bucket(key).insert_with_limit_or_assign(std::forward<K>(key), std::forward<V>(value), num_elements_,
                                                       max_num_elements);
    }

    template <typename K, typename V>
    std::pair<Value, bool> find_or_insert_with_limit(K&& key, V&& value, size_t max_num_elements)
    {
        return bucket(key).find_or_insert_with_limit(std::forward<K>(key), std::forward<V>(value), num_elements_,
                                                     max_num_elements);
    }

    size_t erase(Key const& key)
    {
        const size_t num_deleted = bucket(key).remove_mapping(key);
        if (num_deleted > 0)
            --num_elements_;

        return num_deleted;
    }

    void clear()
    {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        for (unsigned i = 0; i < buckets_.size(); ++i)
        {
            locks.push_back(std::unique_lock<std::shared_mutex>(buckets_[i]->mutex));
        }

        for (unsigned i = 0; i < buckets_.size(); ++i)
        {
            buckets_[i]->data.clear();
        }
        num_elements_ = 0;
    }

    size_t size() const
    {
        return num_elements_;
    }

    template <typename Function>
    void for_each(Function f) const
    {
        std::vector<std::shared_lock<std::shared_mutex>> locks;
        for (unsigned i = 0; i < buckets_.size(); ++i)
        {
            locks.push_back(std::shared_lock<std::shared_mutex>(buckets_[i]->mutex));
        }

        for (unsigned i = 0; i < buckets_.size(); ++i)
        {
            for (auto it = buckets_[i]->data.cbegin(); it != buckets_[i]->data.cend(); ++it)
            {
                f(it->second);
            }
        }
    }

  private:
    bucket_type& bucket(Key const& key) const
    {
        std::size_t const bucket_index = std::hash<Key>{}(key) % buckets_.size();
        return *buckets_[bucket_index];
    }

    std::vector<std::unique_ptr<bucket_type>> buckets_;
    std::atomic_size_t num_elements_ = 0;
};
} // namespace datastore::detail
