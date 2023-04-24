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


class node_view final : private detail::node_observer
{
    friend class vault;

    friend std::ostream& operator<<(std::ostream& lhs, const node_view& rhs);

  public:
    static constexpr size_t max_num_subnodes = 255;
    static constexpr size_t max_num_values = 255;

    [[nodiscard]] node_view(const node_view& other) = delete;

    [[nodiscard]] node_view(node_view&& other) noexcept;

    ~node_view() noexcept;

    node_view& operator=(const node_view& rhs) = delete;

    node_view& operator=(node_view&& rhs) noexcept;

    // Creates a new subnode or opens an existing subnode
    // The subnode can be several levels deep in the volume tree
    std::shared_ptr<node_view> create_subnode(path_view subnode_path);

    // Retrieves the specified subnode
    // The subnode can be several levels deep in the volume tree
    std::shared_ptr<node_view> open_subnode(path_view subnode_path) const;

    // TODO: what if the same node is loaded twice: one time as root and another time as child?
    // TODO: check that the noad was already loaded
    // Creates a subnode and loads the data from the specified node into that subnode
    std::shared_ptr<node_view> load_subnode_tree(path_view subview_name, const std::shared_ptr<node>& subnode);

    // Unloads the specified subnode and its subnodes from the vault
    // This function removes a subnode from the vault but does not modify the volume containing the information.
    size_t unload_subnode_tree(path_view subview_name);

    // Deletes a subnode and any child subnodes recursively
    // The subnode can be several levels deep in the volume tree
    // Can't delete tree roots loaded using load_subnode_tree
    size_t delete_subview_tree(path_view subview_name);

    // Retrieves an array of strings that contains all the subnode names
    // [[nodiscard]] std::unordered_set<std::string> get_subnode_names() const;

    template <typename Function>
    void for_each_subnode(Function f) const
    {
        subviews_.for_each(f);
    }


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
        // for (const std::shared_ptr<node>& node : nodes_)
        // {
        //     auto&& value_names = node->get_values();
        //     values.insert(value_names.begin(), value_names.end());
        // }
        return values;
    }

    std::string_view name();

    [[nodiscard]] std::string path() const;

  private:
    //TODO: depth can be inferred from the path
    //TODO: path_view can be only 32 levels deep, full node path can be deeper
    node_view(const path_view& path, size_t depth);

    void on_create_subnode(const std::shared_ptr<node>& subnode) override;
    void on_delete_subnode(const std::shared_ptr<node>& subnode) override;

    std::string path_;
    size_t depth_;
    striped_hashmap<std::string, std::shared_ptr<node_view>> subviews_;
    sorted_list<std::shared_ptr<node>, decltype(&detail::compare_nodes)> nodes_;
    bool expired_ = false;
};

template <typename T>
[[nodiscard]] std::optional<T> node_view::get_value(const std::string& value_name) const
{
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
    bool success = false;

    // TODO: always inserts a value to the node with highest precedence
    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        success = node->set_value(value_name, new_value);
        return success;
    });

    return success;
}
} // namespace datastore
