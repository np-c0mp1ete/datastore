#include "datastore/node_view.hpp"

#include "datastore/vault.hpp"

namespace datastore
{
namespace detail
{
bool compare_nodes(node* n1, node* n2)
{
    if (n1->priority() != n2->priority())
        return n1->priority() > n2->priority();

    return n1 > n2;
}
} // namespace detail

node_view::node_view(const std::string& name, vault* vault, node_view* parent)
    : name_(name), vault_(vault), parent_(parent), nodes_(&detail::compare_nodes)
{
}

[[nodiscard]] node_view::node_view(const node_view& other) noexcept
    : name_(other.name_), vault_(other.vault_), parent_(other.parent_), subviews_(other.subviews_), nodes_(other.nodes_)
{
    for (auto& [name, subnode] : subviews_)
    {
        subnode.parent_ = this;
    }
}

[[nodiscard]] node_view::node_view(node_view&& other) noexcept
    : name_(std::move(other.name_)), vault_(other.vault_), parent_(other.parent_),
      subviews_(std::move(other.subviews_)), nodes_(std::move(other.nodes_))
{
    for (auto& [name, subnode] : subviews_)
    {
        subnode.parent_ = this;
    }
}

node_view& node_view::operator=(const node_view& rhs) noexcept
{
    if (this == &rhs)
        return *this;

    name_ = rhs.name_;
    vault_ = rhs.vault_;
    parent_ = rhs.parent_;
    subviews_ = rhs.subviews_;
    nodes_ = rhs.nodes_;

    for (auto& [name, subnode] : subviews_)
    {
        subnode.parent_ = this;
    }

    return *this;
}

node_view& node_view::operator=(node_view&& rhs) noexcept
{
    name_ = std::move(rhs.name_);
    vault_ = rhs.vault_;
    parent_ = rhs.parent_;
    subviews_ = std::move(rhs.subviews_);
    nodes_ = std::move(rhs.nodes_);

    for (auto& [name, subnode] : subviews_)
    {
        subnode.parent_ = this;
    }

    return *this;
}

node_view* node_view::create_link_subnode(const std::string& subnode_name, const std::string& target_path)
{
    node_view* subview = create_subnode(subnode_name);
    if (!subview)
        return nullptr;

    subview->set_value("__link", ref{target_path});

    //node_view* target = vault_->root()->open_subview(target_path);

    //subview->nodes_.insert(target->nodes_.begin(), target->nodes_.end());

    return subview;
}

node_view* node_view::create_subnode(const std::string& subnode_name)
{
    //TODO: always takes the node with the highest precedence
    for (const auto& n : nodes_)
    {
        node* subnode = n->create_subnode(subnode_name);
        if (!subnode)
            return nullptr;

        auto [it, inserted] = subviews_.emplace(subnode_name, node_view(subnode_name, vault_, this));

        node_view& subview = it->second;

        subview.nodes_.emplace(subnode);

        return &subview;
    }

    return nullptr;
}



node_view* node_view::open_subview(const std::string& subnode_path)
{
    const size_t dot_pos = subnode_path.find_first_of('.');
    std::string subnode_name = subnode_path.substr(0, dot_pos);

    const auto it = subviews_.find(subnode_name);
    if (it == subviews_.end())
    {
        const ref* link = get_value<ref>("__link");
        if (link)
        {
            std::string new_path = link->path + "." + subnode_path;
            return vault_->root()->open_subview(new_path);
        }
        return nullptr;
    }

    node_view* subnode = &it->second;

    

    if (dot_pos != std::string::npos)
    {
        const std::string remaining_path = subnode_path.substr(dot_pos + 1);
        return subnode->open_subview(remaining_path);
    }

    return subnode;
}

size_t node_view::delete_subnode(const std::string& subnode_name)
{
    size_t num_deleted = 0;
    for (const auto node : nodes_)
    {
        num_deleted += node->delete_subnode(subnode_name);
        subviews_.erase(subnode_name);
    }

    const ref* link = get_value<ref>("__link");
    if (link)
    {
        node_view* target = vault_->root()->open_subview(link->path);
        num_deleted += target->delete_subnode(subnode_name);
    }

    return num_deleted;
}

void node_view::delete_subnode_tree(const std::string& subnode_name)
{
    subviews_.erase(subnode_name);
    for (const auto node : nodes_)
    {
        // TODO: stop iterating when one value is deleted
        node->delete_subnode_tree(subnode_name);
    }
}




void node_view::delete_value(const std::string& value_name)
{
    for (const auto node : nodes_)
    {
        // TODO: stop iterating when one value is deleted
        node->delete_value(value_name);
    }
}

std::string_view node_view::name()
{
    return name_;
}

[[nodiscard]] std::string node_view::path() const
{
    std::string path = name_;
    node_view* parent = parent_;
    while (parent)
    {
        path = parent->name_ + "." + path;
        parent = parent->parent_;
    }
    return path;
}

std::ostream& operator<<(std::ostream& lhs, const node_view& rhs)
{
    lhs << rhs.path() << std::endl;

    for (const auto node : rhs.nodes_)
    {
        for (auto value_name : node->get_value_names())
        {
            lhs << "--> " << static_cast<size_t>(node->priority()) << ": " << node->path() << "." << value_name << " = ";

            std::string name = std::string(value_name);
            const value_kind kind = node->get_value_kind(name);
            if (kind == value_kind::u32) lhs << *node->get_value<uint32_t>(name);
            if (kind == value_kind::u64) lhs << *node->get_value<uint64_t>(name);
            if (kind == value_kind::f32) lhs << *node->get_value<float>(name);
            if (kind == value_kind::f64) lhs << *node->get_value<double>(name);
            if (kind == value_kind::str) lhs << *node->get_value<std::string>(name);
            if (kind == value_kind::ref) lhs << node->get_value<ref>(name)->path;
            lhs << std::endl;
        }
    }

    for (const auto& [name, subview] : rhs.subviews_)
    {
        lhs << subview;
    }

    return lhs;
}

void node_view::set_vault(vault* vault)
{
    vault_ = vault;

    for (auto& [name, subview] : subviews_)
    {
        set_vault(vault);
    }
}

node_view* node_view::assign_subnode(const std::string& subnode_name, node* subnode)
{
    auto [it, inserted] = subviews_.emplace(subnode_name, node_view(subnode_name, vault_, this));

    node_view& subview = it->second;

    subview.nodes_.emplace(subnode);

    auto subnode_names = subnode->get_subnode_names();
    for each (auto name in subnode_names)
    {
        subview.assign_subnode(std::string(name), subnode->open_subnode(std::string(name)));
    }

    return &subview;
}
}
