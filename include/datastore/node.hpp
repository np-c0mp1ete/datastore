#pragma once

#include <optional>
#include <ostream>
#include <string>
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

#define DATASTORE_UNUSED(x) (void)(x)

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
    _count
};

using binary_blob_t = std::vector<uint8_t>;

using value_type = std::variant<uint32_t, uint64_t, float, double, std::string, binary_blob_t>;

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

class attr final
{
  public:
    attr(std::string name, value_type value) : name_(std::move(name)), value_(std::move(value))
    {
    }

    template <typename T, typename = std::enable_if_t<detail::allowed<T>::value>>
    [[nodiscard]] std::optional<T> get_value() const
    {
        const T* value = std::get_if<T>(&value_);
        return value ? std::make_optional(*value) : std::nullopt;
    }

    [[nodiscard]] std::optional<value_kind> get_value_kind() const
    {
        return static_cast<value_kind>(value_.index());
    }

    std::string_view name() const
    {
        return name_;
    }

    value_type value() const
    {
        return value_;
    }

  private:
    std::string name_;
    value_type value_;
};

class node final
{
    friend class detail::serializer;
    friend class volume;
    friend class detail::node_observer;
    friend class node_view;

    friend std::ostream& operator<<(std::ostream& lhs, const node& rhs);

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
    bool delete_subnode_tree();

    // Leave it up to the user to ensure that they don't cause deadlock
    // by acquiring locks in the user-supplied operations
    // and don't cause data races by storing the references for access outside the locks.
    template <typename Function>
    void for_each_subnode(Function f) const;


    // Deletes the specified value from this node
    size_t delete_value(const std::string& value_name);
    void delete_values();

    // Retrieves the value associated with the specified name
    template <typename T, typename = std::enable_if_t<detail::allowed<T>::value>>
    [[nodiscard]] std::optional<T> get_value(const std::string& value_name) const;

    // Retrieves the data type of the value associated with the specified name
    [[nodiscard]] std::optional<value_kind> get_value_kind(const std::string& value_name) const;

    // Sets the value of a name/value pair in the node
    template <typename T, typename = std::enable_if_t<std::is_constructible_v<value_type, T>>>
    bool set_value(const std::string& value_name, T&& new_value);

    // Leave it up to the user to ensure that they don't cause deadlock
    // by acquiring locks in the user-supplied operations
    // and don't cause data races by storing the references for access outside the locks.
    template <typename Function>
    void for_each_value(Function f) const;

    std::string_view name() const;

    [[nodiscard]] path_view path() const;

    [[nodiscard]] uint8_t priority() const;

    [[nodiscard]] bool deleted() const;

private:
    node(path_view full_path, uint8_t volume_priority);

    void register_observer(const std::shared_ptr<detail::node_observer>& observer);
    void notify_on_delete_subnode_observers(const std::shared_ptr<node>& subnode);

  private:
    std::string full_path_str_;
    path_view full_path_view_;
    uint8_t volume_priority;
    detail::striped_hashmap<std::string, std::shared_ptr<node>> subnodes_;
    detail::striped_hashmap<std::string, attr> values_;
    detail::sorted_list<std::weak_ptr<detail::node_observer>, std::owner_less<>> observers_;
    std::atomic_bool deleted_ = false;
};

template <typename Function>
void node::for_each_subnode(Function f) const
{
    if (deleted_)
        return;

    subnodes_.for_each(f);
}

template <typename Function>
void node::for_each_value(Function f) const
{
    if (deleted_)
        return;

    values_.for_each(f);
}

template <typename T, typename>
[[nodiscard]] std::optional<T> node::get_value(const std::string& value_name) const
{
    if (deleted_)
        return std::nullopt;

    const auto opt = values_.find(value_name);
    if (!opt)
        return std::nullopt;

    return opt->get_value<T>();
}

template <typename T, typename>
bool node::set_value(const std::string& value_name, T&& new_value)
{
    if (deleted_)
        return false;

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

    attr a(value_name, std::move(value));
    return values_.insert_with_limit_or_assign(value_name, std::move(a), max_num_values);
}
} // namespace datastore
