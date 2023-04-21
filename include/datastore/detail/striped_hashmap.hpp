#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

template <typename Key, typename Value>
class striped_hashmap
{
  private:
    class bucket_type
    {
      private:
        friend class striped_hashmap;

        typedef std::pair<Key, Value> bucket_value;
        typedef std::list<bucket_value> bucket_data;
        typedef typename bucket_data::iterator bucket_iterator;

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
            std::shared_lock<std::shared_mutex> lock(mutex);
            auto found_entry = find_entry_for(key);
            return (found_entry == data.end()) ? std::nullopt : std::make_optional<Value>(found_entry->second);
        }

        bool insert_with_limit_or_assign(Key const& key, Value const& value, std::atomic_size_t& cur_size, size_t max_size)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
            auto found_entry = find_entry_for(key);
            if (found_entry == data.end())
            {
                if (cur_size > max_size)
                    return false;

                ++cur_size;
                data.push_back(bucket_value(key, value));
            }
            else
            {
                found_entry->second = value;
            }
            return true;
        }

        size_t remove_mapping(Key const& key)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
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
    typedef Key key_type;
    typedef Value mapped_type;

    striped_hashmap(unsigned num_buckets = 257)
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

    bool insert_with_limit_or_assign(Key const& key, Value const& value, size_t max_num_elements)
    {
        return bucket(key).insert_with_limit_or_assign(key, value, num_elements_, max_num_elements);
    }

    size_t erase(Key const& key)
    {
        const size_t num_deleted = bucket(key).remove_mapping(key);
        if (num_deleted > 0)
            --num_elements_;
        return num_deleted;
    }

    size_t size() const
    {
        return num_elements_;
    }

    [[nodiscard]] std::map<Key, Value> get_map() const
    {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        for (unsigned i = 0; i < buckets_.size(); ++i)
        {
            locks.push_back(std::unique_lock<std::shared_mutex>(buckets_[i]->mutex));
        }
        std::map<Key, Value> res;
        for (unsigned i = 0; i < buckets_.size(); ++i)
        {
            for (auto it = buckets_[i]->data.begin(); it != buckets_[i]->data.end(); ++it)
            {
                res.insert(*it);
            }
        }
        return res;
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
