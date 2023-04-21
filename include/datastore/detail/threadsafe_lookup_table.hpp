#pragma once

#include <functional>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <utility>
#include <vector>

template <typename Key, typename Value, typename Hash = std::hash<Key>>
class threadsafe_lookup_table
{
  private:
    class bucket_type
    {
      private:
        friend class threadsafe_lookup_table;

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
            return std::find_if(data.begin(), data.end(), [&](bucket_value const& item) { return item.first == key; });
        }

      public:
        std::optional<Value> value_for(Key const& key) const
        {
            std::shared_lock<std::shared_mutex> lock(mutex);
            auto found_entry = find_entry_for(key);
            return (found_entry == data.end()) ? std::nullopt : std::make_optional<Value>(found_entry->second);
        }

        void add_or_update_mapping(Key const& key, Value const& value)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
            auto found_entry = find_entry_for(key);
            if (found_entry == data.end())
            {
                data.push_back(bucket_value(key, value));
            }
            else
            {
                found_entry->second = value;
            }
        }

        void remove_mapping(Key const& key)
        {
            std::unique_lock<std::shared_mutex> lock(mutex);
            auto found_entry = find_entry_for(key);
            if (found_entry != data.end())
            {
                data.erase(found_entry);
            }
        }
    };

    std::vector<std::unique_ptr<bucket_type>> buckets;
    Hash hasher;

    bucket_type& get_bucket(Key const& key) const
    {
        std::size_t const bucket_index = hasher(key) % buckets.size();
        return *buckets[bucket_index];
    }

  public:
    typedef Key key_type;
    typedef Value mapped_type;
    typedef Hash hash_type;

    threadsafe_lookup_table(unsigned num_buckets = 19, Hash const& hasher_ = Hash())
        : buckets(num_buckets), hasher(hasher_)
    {
        for (unsigned i = 0; i < num_buckets; ++i)
        {
            buckets[i].reset(new bucket_type);
        }
    }

    threadsafe_lookup_table(threadsafe_lookup_table const& other) = delete;
    threadsafe_lookup_table(threadsafe_lookup_table&& other) = default;

    threadsafe_lookup_table& operator=(threadsafe_lookup_table const& other) = delete;
    threadsafe_lookup_table& operator=(threadsafe_lookup_table&& other) = default;

    [[nodiscard]] std::optional<Value> find(Key const& key) const
    {
        return get_bucket(key).value_for(key);
    }

    void emplace(Key const& key, Value const& value)
    {
        get_bucket(key).add_or_update_mapping(key, value);
    }

    size_t erase(Key const& key)
    {
        get_bucket(key).remove_mapping(key);
        return 1;
    }

    // TODO: have an atomic int?
    size_t size() const
    {
        return 0;
    }

    [[nodiscard]] std::map<Key, Value> get_map() const
    {
        std::vector<std::unique_lock<std::shared_mutex>> locks;
        for (unsigned i = 0; i < buckets.size(); ++i)
        {
            locks.push_back(std::unique_lock<std::shared_mutex>(buckets[i]->mutex));
        }
        std::map<Key, Value> res;
        for (unsigned i = 0; i < buckets.size(); ++i)
        {
            for (auto it = buckets[i]->data.begin(); it != buckets[i]->data.end(); ++it)
            {
                res.insert(*it);
            }
        }
        return res;
    }
};
