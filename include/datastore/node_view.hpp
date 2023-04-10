#pragma once

#include "datastore/node.hpp"

#include <set>

namespace datastore
{
class vault;

namespace detail
{
bool compare_nodes(node* n1, node* n2);
}

class node_view
{
  public:
    node_view(const std::string& name, vault* vault, node_view* parent);

    [[nodiscard]] node_view(const node_view& other) noexcept;

    [[nodiscard]] node_view(node_view&& other) noexcept;

    node_view& operator=(const node_view& rhs) noexcept;

    node_view& operator=(node_view&& rhs) noexcept;

    node_view* create_link_subnode(const std::string& subnode_name, const std::string& target_path);

    // Creates a new subnode or opens an existing subnode
    node_view* create_subnode(const std::string& subnode_name);

    // Retrieves the specified subnode
    node_view* open_subview(const std::string& subnode_path);

    // Deletes the specified subnode
    size_t delete_subnode(const std::string& subnode_name);

    // Deletes a subnode and any child subnodes recursively
    void delete_subnode_tree(const std::string& subnode_name);

    // Changes the name of the specified subnode
    void rename_subnode(const std::string& subnode_name, const std::string& new_subnode_name);




    // Deletes the specified value from this node
    void delete_value(const std::string& value_name);

    // Retrieves the value associated with the specified name
    template <typename T>
    [[nodiscard]] auto get_value(const std::string& value_name) const;

    // Retrieves the data type of the value associated with the specified name
    value_kind get_value_kind(const std::string& value_name);

    // Sets the value of a name/value pair in the node
    template <typename T>
    std::enable_if_t<std::is_constructible_v<value_type, T>> set_value(const std::string& value_name, T new_value);

    // Retrieves an array of strings that contains all the value names associated with this node
    std::vector<std::string_view> get_value_names();

    std::string_view name();

    [[nodiscard]] std::string path() const;

    friend std::ostream& operator<<(std::ostream& lhs, const node_view& rhs);

private:
    friend class vault;

    void set_vault(vault* vault);

    node_view* assign_subnode(const std::string& subnode_name, node* subnode);

    std::string name_;
    vault* vault_;
    node_view* parent_;
    std::unordered_map<std::string, node_view> subviews_;
    std::set<node*, decltype(&detail::compare_nodes)> nodes_;
};

template <typename T>
[[nodiscard]] auto node_view::get_value(const std::string& value_name) const
{
    const T* value = nullptr;
    for (const auto node : nodes_)
    {
        value = node->get_value<T>(value_name);
        if (value)
            return value;
    }

    return value;
}

template <typename T>
std::enable_if_t<std::is_constructible_v<value_type, T>> node_view::set_value(const std::string& value_name,
                                                                              T new_value)
{
    // TODO: always inserts a value to the node with highest precedence
    for (auto node : nodes_)
    {
        node->set_value(value_name, new_value);
        return;
    }
}
} // namespace datastore
