#pragma once

#include "datastore/detail/sorted_list.hpp"
#include "datastore/detail/striped_hashmap.hpp"
#include "datastore/node.hpp"

namespace datastore
{
class vault;

namespace detail
{
// Compares nodes based on volume priority
bool compare_nodes(const std::shared_ptr<node>& n1, const std::shared_ptr<node>& n2);
}

class node_view final : public detail::node_observer
{
    friend class vault;

    friend std::ostream& operator<<(std::ostream& lhs, const node_view& rhs);

  public:
    // Maximum number of subviews a node_view can have
    static constexpr size_t max_num_subviews = 10;

    // Maximum number of values a node_view can observe
    static constexpr size_t max_num_values = 10;

    node_view(const node_view& other) = delete;

    node_view(node_view&& other) noexcept;

    ~node_view() noexcept override = default;

    node_view& operator=(const node_view& rhs) = delete;

    node_view& operator=(node_view&& rhs) noexcept;

    // Creates a new subnode or opens an existing subnode
    // The subnode can be several levels deep in the volume tree
    std::shared_ptr<node_view> create_subnode(path_view subnode_path);

    // Retrieves the specified subnode
    // The subnode can be several levels deep in the volume tree
    std::shared_ptr<node_view> open_subnode(path_view subview_path) const;

    // Creates a subnode and loads the data from the specified node into that subnode
    std::shared_ptr<node_view> load_subnode_tree(const std::shared_ptr<node>& subnode);

    // Unloads the specified subnode and its subnodes from the vault
    // This function removes a subnode from the vault but does not modify the volume containing the information.
    bool unload_subnode_tree(path_view subview_name);
    void unload_subnode_tree();

    // Deletes a subnode and any child subnodes recursively
    bool delete_subview_tree(path_view subview_name);

    // Deletes all subnodes and any child subnodes recursively
    bool delete_subview_tree();

    // Iterates over subnodes that exist in the nodes being observed
    template <typename Function>
    void for_each_subnode(Function f) const;

    // Deletes the specified value from this node
    size_t delete_value(const std::string& value_name);

    // Deletes all values from this node
    void delete_values();

    // Retrieves the value associated with the specified name
    template <typename T>
    [[nodiscard]] std::optional<T> get_value(const std::string& value_name) const;

    // Retrieves the data type of the value associated with the specified name
    [[nodiscard]] std::optional<value_kind> get_value_kind(const std::string& value_name) const;

    // Sets the value of a name/value pair in the node
    template <typename T, typename = std::enable_if_t<std::is_constructible_v<value_type, T>>>
    bool set_value(const std::string& value_name, T&& new_value);

    // Iterates over values stored in the nodes observed by this node view
    // Function must have a following signature: void func(const datastore::attr&);
    template <typename Function>
    void for_each_value(Function f) const;

    // Gets the name of the node_view
    [[nodiscard]] std::string_view name() const;

    // Gets the full path of the node_view from the root of the vault
    [[nodiscard]] path_view path() const;

    // Checks whether the client reference is still valid,
    // i.e. the node view observes at least one node that belongs to a volume
    [[nodiscard]] bool expired() const;

  private:
    node_view(path_view full_path);

    void on_create_subnode(const std::shared_ptr<node>& subnode) override;
    void on_delete_subnode(const std::shared_ptr<node>& subnode) override;

    std::string full_path_str_; // Holds a string which is accessed by a path_view object below
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

    // Take care of a case when multiple nodes might have a value with the same name
    // by storing values in a map first
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

    // Return a value from a node based on node/volume priority
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

    // Inserts a value to the node with highest priority
    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        success = node->set_value(value_name, new_value);
        return success;
    });

    return success;
}
} // namespace datastore
