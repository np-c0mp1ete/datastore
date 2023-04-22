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

void node_observer::on_create_subnode(node* subnode) const
{
    auto [it, inserted] = watcher_->subviews_.emplace(
        subnode->name(), node_view(std::string(subnode->name()), watcher_->vault_, watcher_));

    node_view& subview = it->second;

    if (!inserted)
    {
        subview.invalid_ = false;
    }

    subview.nodes_.emplace(subnode);
}

void node_observer::on_delete_subnode(node* subnode) const
{
    //TODO: it's incorrect to check node name, node_views might have a different name
    auto it = watcher_->subviews_.find(subnode->name_);
    node_view& subview = it->second;
    subview.nodes_.erase(subnode);
    if (subview.nodes_.empty())
        subview.invalid_ = true;
}
} // namespace detail

node_view::node_view(const std::string& name, vault* vault, node_view* parent)
    : name_(name), vault_(vault), parent_(parent), nodes_(&detail::compare_nodes), observer_(this)
{
}

//TODO: should node views be copyable?
[[nodiscard]] node_view::node_view(const node_view& other) noexcept
    : name_(other.name_), vault_(other.vault_), parent_(other.parent_), subviews_(other.subviews_),
      nodes_(other.nodes_), observer_(this)
{
    for (auto& [name, subnode] : subviews_)
    {
        subnode.parent_ = this;
    }

    for (auto node : nodes_)
    {
        node->register_observer(&observer_);
    }
}

[[nodiscard]] node_view::node_view(node_view&& other) noexcept
    : name_(std::move(other.name_)), vault_(other.vault_), parent_(other.parent_),
      subviews_(std::move(other.subviews_)), nodes_(std::move(other.nodes_)), observer_(this)
{
    for (auto& [name, subnode] : subviews_)
    {
        subnode.parent_ = this;
    }

    for (auto node : nodes_)
    {
        node->register_observer(&observer_);
    }
}

node_view::~node_view() noexcept
{
    for (auto node : nodes_)
    {
        node->unregister_observer(&observer_);
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

    for (auto node : nodes_)
    {
        node->register_observer(&observer_);
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

    for (auto node : nodes_)
    {
        node->register_observer(&observer_);
    }

    return *this;
}

node_view* node_view::create_subnode(path_view subnode_path)
{
    if (!subnode_path.valid() || invalid_)
        return nullptr;

    size_t depth = 0;
    const node_view* parent = parent_;
    while (parent)
    {
        depth++;
        parent = parent->parent_;
    }
    if (depth > vault::max_tree_depth)
        return nullptr;

    const std::string subnode_name = std::string(*subnode_path.front());

    if (const auto it = subviews_.find(subnode_name); it == subviews_.end() && subviews_.size() > max_num_subnodes)
        return nullptr;

    // TODO: always takes the node with the highest precedence
    node* n = *nodes_.begin();
    node* subnode = n->create_subnode(std::move(subnode_path));
    if (!subnode)
        return nullptr;

    auto [it, inserted] = subviews_.emplace(subnode_name, node_view(subnode_name, vault_, this));

    node_view& subview = it->second;

    if (!inserted)
    {
        subview.invalid_ = false;
    }

    subview.nodes_.emplace(subnode);

    return &subview;
}

node_view* node_view::open_subnode(path_view subnode_path)
{
    if (!subnode_path.valid() || invalid_)
        return nullptr;

    const std::string subnode_name = std::string(*subnode_path.front());

    const auto it = subviews_.find(subnode_name);
    if (it == subviews_.end())
    {
        return nullptr;
    }

    node_view*  subnode = &it->second;

    if (subnode->invalid_)
        return nullptr;

    if (subnode_path.composite())
    {
        subnode_path.pop_front();
        return subnode->open_subnode(std::move(subnode_path));
    }

    return subnode;
}

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

    return num_deleted;
}

std::unordered_set<std::string> node_view::get_subnode_names() const
{
    // Copy strings to avoid scenarios when a subnode gets deleted
    // and the caller is left with a dangling pointer
    std::unordered_set<std::string> names;
    for (const auto& [name, subnode] : subviews_)
    {
        if (subnode.invalid_)
            continue;
        names.insert(name);
    }
    return names;
}

size_t node_view::delete_value(const std::string& value_name)
{
    for (const auto node : nodes_)
    {
        const size_t num_deleted = node->delete_value(value_name);
        if (num_deleted > 0)
            return num_deleted;
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

std::string_view node_view::name()
{
    if (invalid_)
        return "";
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
        auto values = node->values_.get_map();
        for (const auto& [name, value] : values)
        {
            lhs << "--> " << static_cast<size_t>(node->priority()) << ": " << node->path() << "." << name << " = "
                << value << std::endl;
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

    //TODO: cleanup
    node_view* target_parent_subnode = this;

    if (!target_parent_subnode)
        return nullptr;

    size_t depth = 0;
    const node_view* parent = target_parent_subnode->parent_;
    while (parent)
    {
        depth++;
        parent = parent->parent_;
    }
    if (depth > vault::max_tree_depth)
        return nullptr;

    if (const auto it = target_parent_subnode->subviews_.find(target_subnode_name);
        it == target_parent_subnode->subviews_.end() && target_parent_subnode->subviews_.size() > max_num_subnodes)
        return nullptr;

    auto [it, inserted] = target_parent_subnode->subviews_.emplace(
        target_subnode_name, node_view(target_subnode_name, vault_, target_parent_subnode));

    node_view& subview = it->second;

    const auto subnode_names = subnode->get_subnode_names();
    for (const auto& name : subnode_names)
    {
        if (!subview.load_subnode(name, subnode->open_subnode(name)))
        {
            // Undo parent creation
            //TODO: add tests for this (and use cpp coverage to find similar code paths
            target_parent_subnode->subviews_.erase(target_subnode_name);
            return nullptr;
        }
    }

    subview.nodes_.emplace(subnode);
    subnode->register_observer(&subview.observer_);

    return &subview;
}

size_t node_view::unload_subnode(path_view subnode_path)
{
    if (!subnode_path.valid())
        return 0;

    node_view* target_subnode = open_subnode(std::move(subnode_path));
    if (!target_subnode)
        return 0;

    size_t num_unloaded = 0;
    for (auto& [name, subnode] : target_subnode->subviews_)
    {
        num_unloaded += target_subnode->unload_subnode(name);
    }

    num_unloaded += target_subnode->nodes_.size();
    target_subnode->nodes_.clear();

    // Not deleting subviews to avoid dangling pointers on clients' hands
    target_subnode->invalid_ = true;

    return num_unloaded;
}
} // namespace datastore
