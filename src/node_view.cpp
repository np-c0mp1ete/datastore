#include "datastore/node_view.hpp"

#include "datastore/vault.hpp"

namespace datastore
{
namespace detail
{
bool compare_nodes(const node* n1, const node* n2)
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

// node_view* node_view::create_symlink_subnode(path_view subnode_name, const path_view& target_path)
// {
//     if (!subnode_name.valid() || !target_path.valid())
//         return nullptr;
//
//     node_view* subview = create_subnode(std::move(subnode_name));
//     if (!subview)
//         return nullptr;
//
//     subview->set_value("__link", ref{target_path.str()});
//
//     //node_view* target = vault_->root()->open_subnode(target_path);
//
//     //subview->nodes_.insert(target->nodes_.begin(), target->nodes_.end());
//
//     return subview;
// }

node_view* node_view::create_subnode(path_view subnode_path)
{
    if (!subnode_path.valid())
        return nullptr;

    //TODO: always takes the node with the highest precedence
    node* n = *nodes_.begin();
    const std::string subnode_name = std::string(subnode_path.front().value());
    node* subnode = n->create_subnode(std::move(subnode_path));
    if (!subnode)
        return nullptr;

    auto [it, inserted] = subviews_.emplace(subnode_name, node_view(subnode_name, vault_, this));

    node_view& subview = it->second;

    subview.nodes_.emplace(subnode);

    return &subview;
}

node_view* node_view::open_subnode(path_view subnode_path)
{
    if (!subnode_path.valid())
        return nullptr;

    const std::string subnode_name = std::string(subnode_path.front().value());

    const auto it = subviews_.find(subnode_name);
    if (it == subviews_.end())
    {
        auto link = get_value<ref>("__link");
        if (link)
        {
            const std::string new_path = link->path + path_view::path_separator + subnode_path.str();
            return vault_->root()->open_subnode(new_path);
        }
        return nullptr;
    }

    node_view* subnode = &it->second;

    if (subnode_path.composite())
    {
        subnode_path.pop_front();
        return subnode->open_subnode(std::move(subnode_path));
    }

    return subnode;
}

// size_t node_view::delete_subnode(path_view subnode_path)
// {
//     if (!subnode_path.valid())
//         return 0;
//
//     size_t num_deleted = 0;
//     for (const auto node : nodes_)
//     {
//         num_deleted += node->delete_subnode(subnode_path);
//         subviews_.erase(std::string(subnode_path.front().value()));
//     }
//
//     const ref* link = get_value<ref>("__link");
//     if (link)
//     {
//         node_view* target = vault_->root()->open_subnode(link->path);
//         num_deleted += target->delete_subnode(subnode_path);
//     }
//
//     return num_deleted;
// }

size_t node_view::delete_subnode_tree(path_view subnode_path)
{
    if (!subnode_path.valid())
        return 0;

    const std::string target_subnode_name = std::string(*subnode_path.back());

    node_view* target_parent_subnode;
    if (subnode_path.composite())
    {
        subnode_path.pop_back();
        target_parent_subnode = open_subnode(std::move(subnode_path));
    }
    else
    {
        target_parent_subnode = this;
    }

    if (!target_parent_subnode)
        return 0;

    size_t num_deleted = 0;
    for (const auto node : target_parent_subnode->nodes_)
    {
        num_deleted += node->delete_subnode_tree(target_subnode_name);
    }

    target_parent_subnode->subviews_.erase(target_subnode_name);

    // const ref* link = get_value<ref>("__link");
    // if (link)
    // {
    //     node_view* target = vault_->root()->open_subnode(link->path);
    //     num_deleted += target->delete_subnode_tree(subnode_path);
    // }

    return num_deleted;
}

std::unordered_set<std::string> node_view::get_subnode_names() const
{
    // Copy strings to avoid scenarios when a subnode gets deleted
    // and the caller is left with a dangling pointer
    std::unordered_set<std::string> names;
    for (const auto& [name, subnode] : subviews_)
    {
        names.insert(name);
    }
    return names;
}


size_t node_view::delete_value(const std::string& value_name)
{
    for (const auto node : nodes_)
    {
        // TODO: stop iterating when one value is deleted
        return node->delete_value(value_name);
    }

    return 0;
}

std::optional<value_kind> node_view::get_value_kind(const std::string& value_name) const
{
    for (const auto node : nodes_)
    {
        if (const auto kind = node->get_value_kind(value_name))
            return kind;
    }

    return std::nullopt;
}

std::unordered_set<std::string> node_view::get_value_names() const
{
    // Copy strings to avoid scenarios when a subnode gets deleted
    // and the caller is left with a dangling pointer
    std::unordered_set<std::string> names;
    for (const auto node : nodes_)
    {
        auto&& value_names = node->get_value_names();
        names.insert(value_names.begin(), value_names.end());
    }
    return names;
}

std::string_view node_view::name()
{
    return name_;
}

[[nodiscard]] std::string node_view::path() const
{
    std::string path = name_;
    const node_view* parent = parent_;
    while (parent)
    {
        path = parent->name_ + path_view::path_separator + path;
        parent = parent->parent_;
    }
    return path;
}

std::ostream& operator<<(std::ostream& lhs, const node_view& rhs)
{
    lhs << rhs.path() << std::endl;

    for (const auto node : rhs.nodes_)
    {
        for (const auto& [name, value] : node->values_)
        {
            lhs << "--> " << static_cast<size_t>(node->priority()) << ": " << node->path() << "."  << name << " = " << value
                << std::endl;
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

node_view* node_view::load_subnode(path_view subnode_name, node* subnode)
{
    if (!subnode_name.valid() || subnode_name.composite())
        return nullptr;

    std::string target_subnode_name = std::string(*subnode_name.back());

    node_view* target_parent_subnode = this;

    if (!target_parent_subnode)
        return nullptr;

    auto [it, inserted] = target_parent_subnode->subviews_.emplace(
        target_subnode_name, node_view(target_subnode_name, vault_, target_parent_subnode));

    node_view& subview = it->second;

    subview.nodes_.emplace(subnode);

    const auto subnode_names = subnode->get_subnode_names();
    for (const auto& name : subnode_names)
    {
        subview.load_subnode(name, subnode->open_subnode(name));
    }

    return &subview;
}
}
