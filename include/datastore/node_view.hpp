#pragma once

#include "datastore/node.hpp"
#include "datastore/detail/striped_hashmap.hpp"
#include "datastore/detail/sorted_list.hpp"

namespace datastore
{
class vault;

namespace detail
{
bool compare_nodes(const std::shared_ptr<node>& n1, const std::shared_ptr<node>& n2);
}

class node_view final : detail::node_observer
{
    friend class vault;

    friend std::ostream& operator<<(std::ostream& lhs, const node_view& rhs);

  public:
    static constexpr size_t max_num_subviews = 10;
    static constexpr size_t max_num_values = 10;

    node_view(const node_view& other) = delete;

    node_view(node_view&& other) noexcept;

    ~node_view() noexcept override;

    node_view& operator=(const node_view& rhs) = delete;

    node_view& operator=(node_view&& rhs) noexcept;

    // Creates a new subnode or opens an existing subnode
    // The subnode can be several levels deep in the volume tree
    std::shared_ptr<node_view> create_subnode(path_view subnode_path);

    // Retrieves the specified subnode
    // The subnode can be several levels deep in the volume tree
    std::shared_ptr<node_view> open_subnode(path_view subview_path) const;

    // TODO: what if the same node is loaded twice: one time as root and another time as child?
    // TODO: check that the node was already loaded
    // Creates a subnode and loads the data from the specified node into that subnode
    std::shared_ptr<node_view> load_subnode_tree(path_view subview_name, const std::shared_ptr<node>& subnode);

    // Unloads the specified subnode and its subnodes from the vault
    // This function removes a subnode from the vault but does not modify the volume containing the information.
    bool unload_subnode_tree(path_view subview_name);
    void unload_subnode_tree();

    // Deletes a subnode and any child subnodes recursively
    // The subnode can be several levels deep in the volume tree
    // Can't delete tree roots loaded using load_subnode_tree
    bool delete_subview_tree(path_view subview_name);
    bool delete_subview_tree();

    template <typename Function>
    void for_each_subnode(Function f) const;


    // Deletes the specified value from this node
    size_t delete_value(const std::string& value_name);
    void delete_values();

    // Retrieves the value associated with the specified name
    template <typename T>
    [[nodiscard]] std::optional<T> get_value(const std::string& value_name) const;

    // Retrieves the data type of the value associated with the specified name
    [[nodiscard]] std::optional<value_kind> get_value_kind(const std::string& value_name) const;

    // Sets the value of a name/value pair in the node
    template <typename T, typename = std::enable_if_t<std::is_constructible_v<value_type, T>>>
    bool set_value(const std::string& value_name, T&& new_value);

    template <typename Function>
    void for_each_value(Function f) const;

    [[nodiscard]] std::string_view name() const;

    [[nodiscard]] path_view path() const;

    [[nodiscard]] bool expired() const;

  private:
    node_view(path_view full_path);

    void on_create_subnode(const std::shared_ptr<node>& subnode) override;
    void on_delete_subnode(const std::shared_ptr<node>& subnode) override;

    std::string full_path_str_;
    path_view full_path_view_;
    detail::striped_hashmap<std::string, std::shared_ptr<node_view>> subviews_;
    detail::sorted_list<std::shared_ptr<node>, decltype(&detail::compare_nodes)> nodes_;
    std::atomic_bool expired_ = false;
};

template <typename Function>
void node_view::for_each_subnode(Function f) const
{
    if (expired_)
        return;

    subviews_.for_each(f);
}

template <typename Function>
void node_view::for_each_value(Function f) const
{
    if (expired_)
        return;

    std::unordered_map<std::string, attr> values;

    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->for_each_value([&](const attr& a) {
            values.emplace(a.name(), a);
        });
    });

    for (const auto& [key, value] : values)
        f(value);
}

template <typename T>
[[nodiscard]] std::optional<T> node_view::get_value(const std::string& value_name) const
{
    if (expired_)
        return std::nullopt;

    std::optional<T> value;

    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        value = node->get_value<T>(value_name);
        return value;
    });

    return value;
}

template <typename T, typename>
bool node_view::set_value(const std::string& value_name, T&& new_value)
{
    if (expired_)
        return false;

    bool success = false;

    // Inserts a value to the node with highest precedence
    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        success = node->set_value(value_name, new_value);
        return success;
    });

    return success;
}
} // namespace datastore
