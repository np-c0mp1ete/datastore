#pragma once

#include "datastore/node.hpp"

#include <set>

namespace datastore
{
class vault;

namespace detail
{
bool compare_nodes(const node* n1, const node* n2);
}

class node_view
{
    friend class vault;

    friend std::ostream& operator<<(std::ostream& lhs, const node_view& rhs);

  public:
    node_view(const std::string& name, vault* vault, node_view* parent);

    [[nodiscard]] node_view(const node_view& other) noexcept;

    [[nodiscard]] node_view(node_view&& other) noexcept;

    node_view& operator=(const node_view& rhs) noexcept;

    node_view& operator=(node_view&& rhs) noexcept;

    // Creates a new subnode as a symbolic link to the target node
    // Target path must be an absolute path from the vault root
    node_view* create_symlink_subnode(path_view subnode_name, const path_view& target_path);

    // Creates a new subnode or opens an existing subnode
    // The subnode can be several levels deep in the volume tree
    node_view* create_subnode(path_view subnode_path);

    // Retrieves the specified subnode
    // The subnode can be several levels deep in the volume tree
    node_view* open_subnode(path_view subnode_path);

    // Creates a subnode and loads the data from the specified node into that subnode
    node_view* load_subnode(path_view subnode_name, node* subnode);

    // Unloads the specified subnode and its subnodes from the vault
    // The subnode referred to by the subnode_path parameter must have been created by using the load_subnode function.
    void unload_subnode(path_view subnode_path);

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

    // Deletes the specified value from this node
    size_t delete_value(const std::string& value_name);

    // Retrieves the value associated with the specified name
    template <typename T>
    [[nodiscard]] std::optional<T> get_value(const std::string& value_name) const;

    // Retrieves the data type of the value associated with the specified name
    // TODO: implement
    std::optional<value_kind> get_value_kind(const std::string& value_name);

    // Sets the value of a name/value pair in the node
    template <typename T, typename = std::enable_if_t<std::is_constructible_v<value_type, T>>>
    void set_value(const std::string& value_name, T new_value);

    // Retrieves an array of strings that contains all the value names associated with this node
    // TODO: implement
    std::vector<std::string_view> get_value_names();

    std::string_view name();

    [[nodiscard]] std::string path() const;

  private:
    void set_vault(vault* vault);

    std::string name_;
    vault* vault_;
    node_view* parent_;
    std::unordered_map<std::string, node_view> subviews_;
    std::set<node*, decltype(&detail::compare_nodes)> nodes_;
};

template <typename T>
[[nodiscard]] std::optional<T> node_view::get_value(const std::string& value_name) const
{
    for (const auto node : nodes_)
    {
        auto value = node->get_value<T>(value_name);
        if (value)
            return value;
    }

    return std::nullopt;
}

template <typename T, typename>
void node_view::set_value(const std::string& value_name, T new_value)
{
    // TODO: always inserts a value to the node with highest precedence
    for (auto node : nodes_)
    {
        node->set_value(value_name, new_value);
        return;
    }
}
} // namespace datastore
