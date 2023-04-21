#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <unordered_map>
#include <variant>

#include "datastore/path_view.hpp"
#include "datastore/detail/threadsafe_lookup_table.hpp"

#include <set>
#include <unordered_set>

namespace datastore
{
class node_view;
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
constexpr size_t max_str_value_size_bytes = 1ull * 1024;
constexpr size_t max_bin_value_size_bytes = 1ull * 1024;

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
class node_observer;
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

class node
{
    friend class detail::serializer;
    friend class volume;
    friend class detail::node_observer;
    friend class node_view;

    friend std::ostream& operator<<(std::ostream& lhs, const node& rhs);
    friend std::ostream& operator<<(std::ostream& lhs, const node_view& rhs);

  public:
    static constexpr size_t max_num_subnodes = 255;
    static constexpr size_t max_num_values = 255;

    // [[nodiscard]] node(const node& other) noexcept;

    [[nodiscard]] node(node&& other) noexcept;

    // node& operator=(const node& rhs) noexcept;

    node& operator=(node&& rhs) noexcept;

    // Creates a new subnode or opens an existing subnode
    // The subnode can be several levels deep in the volume tree
    node* create_subnode(path_view subnode_path);

    // Retrieves the specified subnode
    // The subnode can be several levels deep in the volume tree
    node* open_subnode(path_view subnode_path);

    // Deletes the specified subnode
    // The subnode can be several levels deep in the volume tree
    // The subnode to be deleted must not have subnodes
    // TODO: remove
    // size_t delete_subnode(path_view subnode_path);

    // Deletes a subnode and any child subnodes recursively
    // The subnode can be several levels deep in the volume tree
    size_t delete_subnode_tree(path_view subnode_path);

    // Changes the name of the specified subnode
    // TODO: remove
    // void rename_subnode(const std::string& subnode_name, const std::string& new_subnode_name);

    // Retrieves an array of strings that contains all the subnode names
    std::unordered_set<std::string> get_subnode_names();


    // Deletes the specified value from this node
    size_t delete_value(const std::string& value_name);

    // Retrieves the value associated with the specified name
    template <typename T, typename = std::enable_if_t<detail::allowed<T>::value>>
    [[nodiscard]] std::optional<T> get_value(const std::string& value_name) const;

    // Retrieves the data type of the value associated with the specified name
    [[nodiscard]] std::optional<value_kind> get_value_kind(const std::string& value_name) const;

    // Sets the value of a name/value pair in the node
    template <typename T, typename = std::enable_if_t<std::is_constructible_v<value_type, T>>>
    bool set_value(const std::string& value_name, T&& new_value);

    // Retrieves an array of strings that contains all the value names associated with this node
    //TODO: use for_each pattern instead
    auto get_values() const
    {
        return values_.get_map();
    }

    std::string_view name();

    [[nodiscard]] uint8_t priority() const;

    [[nodiscard]] std::string path() const;


private:
    node(std::string name, volume* volume, node* parent);

    void set_name(const std::string& new_subnode_name);

    void set_volume(volume* volume);

    void register_observer(detail::node_observer* observer);

    void unregister_observer(detail::node_observer* observer);

  private:
    std::string name_;
    volume* volume_;
    node* parent_;
    std::unordered_map<std::string, node> subnodes_;
    threadsafe_lookup_table<std::string, value_type> values_;
    std::list<detail::node_observer*> observers_;
    bool deleted_ = false;
};

template <typename T, typename>
[[nodiscard]] std::optional<T> node::get_value(const std::string& value_name) const
{
    const auto it = values_.find(value_name);
    if (!it)
        return std::nullopt;

    // Return by value to avoid clients ending up with dangling pointers
    auto value = std::get_if<T>(&it.value());
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

    const auto opt = values_.find(value_name);
    if (!opt && values_.size() > max_num_values)
        return false;

    values_.emplace(value_name, std::move(value));
    return true;
}
} // namespace datastore
