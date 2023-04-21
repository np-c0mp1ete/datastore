#pragma once

#include "datastore/node.hpp"

#include <set>

namespace datastore
{
class vault;

namespace detail
{
bool compare_nodes(const node* n1, const node* n2);


class node_observer
{
  public:
    node_observer(node_view* watcher) : watcher_(watcher)
    {
    }

    void on_create_subnode(node* subnode) const;
    void on_delete_subnode(node* subnode) const;

private:
    node_view* watcher_;
};
} // namespace detail


class node_view
{
    friend class vault;
    friend class detail::node_observer;

    friend std::ostream& operator<<(std::ostream& lhs, const node_view& rhs);

  public:
    static constexpr size_t max_num_subnodes = 255;
    static constexpr size_t max_num_values = 255;

    [[nodiscard]] node_view(const node_view& other) noexcept;

    [[nodiscard]] node_view(node_view&& other) noexcept;

    ~node_view() noexcept;

    node_view& operator=(const node_view& rhs) noexcept;

    node_view& operator=(node_view&& rhs) noexcept;

    // Creates a new subnode as a symbolic link to the target node
    // Target path must be an absolute path from the vault root
    // node_view* create_symlink_subnode(path_view subnode_name, const path_view& target_path);

    // Creates a new subnode or opens an existing subnode
    // The subnode can be several levels deep in the volume tree
    node_view* create_subnode(path_view subnode_path);

    // Retrieves the specified subnode
    // The subnode can be several levels deep in the volume tree
    node_view* open_subnode(path_view subnode_path);

    // Creates a subnode and loads the data from the specified node into that subnode
    node_view* load_subnode(path_view subnode_name, node* subnode);

    // Unloads the specified subnode and its subnodes from the vault
    // This function removes a subnode from the vault but does not modify the volume containing the information.
    size_t unload_subnode(path_view subnode_path);

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
    [[nodiscard]] std::unordered_set<std::string> get_subnode_names() const;


    // Deletes the specified value from this node
    size_t delete_value(const std::string& value_name);

    // Retrieves the value associated with the specified name
    template <typename T>
    [[nodiscard]] std::optional<T> get_value(const std::string& value_name) const;

    // Retrieves the data type of the value associated with the specified name
    [[nodiscard]] std::optional<value_kind> get_value_kind(const std::string& value_name) const;

    // Sets the value of a name/value pair in the node
    template <typename T, typename = std::enable_if_t<std::is_constructible_v<value_type, T>>>
    bool set_value(const std::string& value_name, T&& new_value);

    // Retrieves an array of strings that contains all the value names associated with this node
    [[nodiscard]] auto get_values() const
    {
        std::map<std::string, value_type> values;
        for (const auto node : nodes_)
        {
            auto&& value_names = node->get_values();
            values.insert(value_names.begin(), value_names.end());
        }
        return values;
    }

    std::string_view name();

    [[nodiscard]] std::string path() const;

  private:
    node_view(const std::string& name, vault* vault, node_view* parent);

    void set_vault(vault* vault);

    std::string name_;
    vault* vault_;
    node_view* parent_;
    std::unordered_map<std::string, node_view> subviews_;
    std::set<node*, decltype(&detail::compare_nodes)> nodes_;
    bool invalid_ = false;
    detail::node_observer observer_;
};

template <typename T>
[[nodiscard]] std::optional<T> node_view::get_value(const std::string& value_name) const
{
    for (const auto node : nodes_)
    {
        if (auto value = node->get_value<T>(value_name))
            return value;
    }

    return std::nullopt;
}

template <typename T, typename>
bool node_view::set_value(const std::string& value_name, T&& new_value)
{
    // TODO: always inserts a value to the node with highest precedence
    for (auto node : nodes_)
    {
        if (node->set_value(value_name, new_value))
            return true;
    }

    return false;
}
} // namespace datastore
