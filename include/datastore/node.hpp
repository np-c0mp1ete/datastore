#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <variant>

#include "datastore/path_view.hpp"
#include "datastore/detail/striped_hashmap.hpp"
#include "datastore/detail/sorted_list.hpp"


#if defined(DATASTORE_DEBUG) && !defined(NDEBUG)
#include <cassert>
#define DATASTORE_ASSERT(expr) assert(expr)
#else
#define DATASTORE_ASSERT(expr) static_assert(true)
#endif

namespace datastore
{
class node;
class volume;

enum class value_kind : uint8_t
{
    u32,
    u64,
    f32,
    f64,
    str,
    bin,
    ref,
    _count
};

struct ref
{
    std::string path;
};

using binary_blob_t = std::vector<uint8_t>;

//TODO: remove ref type
using value_type = std::variant<uint32_t, uint64_t, float, double, std::string, binary_blob_t, ref>;

constexpr size_t max_value_name_size_bytes = 255;
constexpr size_t max_str_value_size_bytes = 255;
constexpr size_t max_bin_value_size_bytes = 255;

template <class T>
constexpr auto to_underlying(T value) noexcept
{
    return static_cast<std::underlying_type_t<T>>(value);
}

static_assert(std::is_same_v<uint32_t, std::variant_alternative_t<to_underlying(value_kind::u32), value_type>>);
static_assert(std::is_same_v<uint64_t, std::variant_alternative_t<to_underlying(value_kind::u64), value_type>>);
static_assert(std::is_same_v<float, std::variant_alternative_t<to_underlying(value_kind::f32), value_type>>);
static_assert(std::is_same_v<double, std::variant_alternative_t<to_underlying(value_kind::f64), value_type>>);
static_assert(std::is_same_v<std::string, std::variant_alternative_t<to_underlying(value_kind::str), value_type>>);
static_assert(std::is_same_v<binary_blob_t, std::variant_alternative_t<to_underlying(value_kind::bin), value_type>>);
static_assert(std::is_same_v<ref, std::variant_alternative_t<to_underlying(value_kind::ref), value_type>>);
static_assert(std::variant_size_v<value_type> == to_underlying(value_kind::_count));

std::ostream& operator<<(std::ostream& lhs, const value_type& rhs);


namespace detail
{

template <class T, class U>
struct is_one_of;

template <class T, class... Ts>
struct is_one_of<T, std::variant<Ts...>> : std::bool_constant<(std::is_same_v<T, Ts> || ...)>
{
};


template <class T>
using allowed = is_one_of<T, value_type>;

class serializer;

class node_observer
{
  public:
    virtual ~node_observer() = default;

    virtual void on_create_subnode(const std::shared_ptr<node>& subnode) = 0;
    virtual void on_delete_subnode(const std::shared_ptr<node>& subnode) = 0;
};
} // namespace detail

namespace literals
{
constexpr uint32_t operator""_u32(unsigned long long value)
{
    return static_cast<uint32_t>(value);
}

constexpr uint64_t operator""_u64(unsigned long long value)
{
    return static_cast<uint64_t>(value);
}
} // namespace literals

class node final
{
    friend class detail::serializer;
    friend class volume;
    friend class detail::node_observer;
    friend class node_view;

    //friend std::ostream& operator<<(std::ostream& lhs, const node& rhs);
    //friend std::ostream& operator<<(std::ostream& lhs, const node_view& rhs);

  public:
    static constexpr size_t max_num_subnodes = 10;
    static constexpr size_t max_num_values = 10;

    node(const node& other) = delete;
    node(node&& other) noexcept;

    node& operator=(const node& rhs) = delete;
    node& operator=(node&& rhs) noexcept;

    // Creates a new subnode or opens an existing subnode
    // The subnode can be several levels deep in the volume tree
    std::shared_ptr<node> create_subnode(path_view subnode_path);

    // Retrieves the specified subnode
    // The subnode can be several levels deep in the volume tree
    std::shared_ptr<node> open_subnode(path_view subnode_path) const;

    // Deletes a subnode and any child subnodes recursively
    // The subnode can be several levels deep in the volume tree
    bool delete_subnode_tree(path_view subnode_name);

    // Retrieves an array of strings that contains all the subnode names
    // std::unordered_set<std::string> get_subnode_names();

    // Leave it up to the user to ensure that they don't cause deadlock
    // by acquiring locks in the user-supplied operations
    // and don't cause data races by storing the references for access outside the locks.
    template <typename Function>
    void for_each_subnode(Function f) const
    {
        subnodes_.for_each(f);
    }


    // Deletes the specified value from this node
    size_t delete_value(const std::string& value_name);

    // Retrieves the value associated with the specified name
    template <typename T, typename = std::enable_if_t<detail::allowed<T>::value>>
    [[nodiscard]] std::optional<T> get_value(const std::string& value_name) const;

    //TODO: move to a wrapper class for std::variant
    // Retrieves the data type of the value associated with the specified name
    [[nodiscard]] std::optional<value_kind> get_value_kind(const std::string& value_name) const;

    // Sets the value of a name/value pair in the node
    template <typename T, typename = std::enable_if_t<std::is_constructible_v<value_type, T>>>
    bool set_value(const std::string& value_name, T&& new_value);

    // Retrieves an array of strings that contains all the value names associated with this node
    //TODO: use for_each pattern instead
    // auto get_values() const
    // {
    //     return values_.get_map();
    // }

    // Leave it up to the user to ensure that they don't cause deadlock
    // by acquiring locks in the user-supplied operations
    // and don't cause data races by storing the references for access outside the locks.
    template <typename Function>
    void for_each_value(Function f) const
    {
        values_.for_each(f);
    }

    std::string_view name();

    [[nodiscard]] uint8_t priority() const;

    [[nodiscard]] std::string path() const;


private:
    node(std::string name, std::string path, uint8_t volume_priority, size_t depth);

    void register_observer(detail::node_observer* observer);
    void unregister_observer(const detail::node_observer* observer);
    void notify_on_delete_subnode_observers(const std::shared_ptr<node>& subnode) const;

  private:
    std::string name_;
    std::string path_;
    uint8_t volume_priority;
    // node* parent_;
    striped_hashmap<std::string, std::shared_ptr<node>> subnodes_;
    striped_hashmap<std::string, value_type> values_;
    sorted_list<detail::node_observer*> observers_;
    std::atomic_bool deleted_ = false;
    size_t depth_ = 0;
};

template <typename T, typename>
[[nodiscard]] std::optional<T> node::get_value(const std::string& value_name) const
{
    const auto opt = values_.find(value_name);
    if (!opt)
        return std::nullopt;

    const T* value = std::get_if<T>(&opt.value());
    return value ? std::make_optional(*value) : std::nullopt;
}

//TODO: cleanup data size conststraints code
template <typename T, typename>
bool node::set_value(const std::string& value_name, T&& new_value)
{
    if (value_name.size() > max_value_name_size_bytes)
    {
        return false;
    }

    value_type value = std::forward<T>(new_value);
    const value_kind kind = static_cast<value_kind>(value.index());
    if (kind == value_kind::str && std::get<std::string>(value).size() > max_str_value_size_bytes)
    {
        return false;
    }
    if (kind == value_kind::bin && std::get<binary_blob_t>(value).size() > max_bin_value_size_bytes)
    {
        return false;
    }

    return values_.insert_with_limit_or_assign(value_name, std::move(value), max_num_values);
}

std::ostream& operator<<(std::ostream& lhs, const node& rhs);
} // namespace datastore
