#pragma once

#include <ostream>
#include <string>
#include <unordered_map>
#include <variant>

namespace datastore
{
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

using value_type = std::variant<uint32_t, uint64_t, float, double, std::string, binary_blob_t, ref>;

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
  public:
    node(const std::string& name, volume* volume, node* parent);

    [[nodiscard]] node(const node& other) noexcept;

    [[nodiscard]] node(node&& other) noexcept;

    node& operator=(const node& rhs) noexcept;

    node& operator=(node&& rhs) noexcept;

    // Creates a new subnode or opens an existing subnode
    node* create_subnode(const std::string& subnode_path);

    // Retrieves the specified subnode
    node* open_subnode(const std::string& subnode_path);

    // Deletes the specified subnode
    size_t delete_subnode(const std::string& subnode_name);

    // Deletes a subnode and any child subnodes recursively
    void delete_subnode_tree(const std::string& subnode_name);

    // Changes the name of the specified subnode
    void rename_subnode(const std::string& subnode_name, const std::string& new_subnode_name);

    // Retrieves an array of strings that contains all the subnode names
    std::vector<std::string_view> get_subnode_names();


    // Deletes the specified value from this node
    void delete_value(const std::string& value_name);

    // Retrieves the value associated with the specified name
    template <typename T, typename = std::enable_if_t<detail::allowed<T>::value>>
    [[nodiscard]] const T* get_value(const std::string& value_name) const;

    // Retrieves the data type of the value associated with the specified name
    value_kind get_value_kind(const std::string& value_name) const;

    // Sets the value of a name/value pair in the node
    template <typename T>
    std::enable_if_t<std::is_constructible_v<value_type, T>> set_value(const std::string& value_name, T new_value);

    // Retrieves an array of strings that contains all the value names associated with this node
    std::vector<std::string_view> get_value_names();

    std::string_view name();

    [[nodiscard]] uint8_t priority() const;

    [[nodiscard]] std::string path() const;

    friend std::ostream& operator<<(std::ostream& lhs, const node& rhs);

    friend class serializer;
    friend class volume;

private:
    void set_name(const std::string& new_subnode_name);

    void set_volume(volume* volume);

    std::string name_;
    volume* volume_;
    node* parent_;
    std::unordered_map<std::string, node> subnodes_;
    std::unordered_map<std::string, value_type> values_;
};

template <typename T, typename>
[[nodiscard]] const T* node::get_value(const std::string& value_name) const
{
    const auto it = values_.find(value_name);
    if (it == values_.end())
        return nullptr;
    return std::get_if<T>(&it->second);
}

template <typename T>
std::enable_if_t<std::is_constructible_v<value_type, T>> node::set_value(const std::string& value_name, T new_value)
{
    values_[value_name] = new_value;
}
} // namespace datastore
