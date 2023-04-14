#include "datastore/node.hpp"

#include "datastore/volume.hpp"

#include <set>
#include <unordered_set>

namespace datastore
{
std::ostream& operator<<(std::ostream& lhs, const value_type& rhs)
{
    const auto kind = static_cast<value_kind>(rhs.index());
    if (kind == value_kind::u32)
        lhs << std::get<uint32_t>(rhs);
    if (kind == value_kind::u64)
        lhs << std::get<uint64_t>(rhs);
    if (kind == value_kind::f32)
        lhs << std::get<float>(rhs);
    if (kind == value_kind::f64)
        lhs << std::get<double>(rhs);
    if (kind == value_kind::str)
        lhs << std::get<std::string>(rhs);
    if (kind == value_kind::ref)
        lhs << std::get<ref>(rhs).path;

    return lhs;
}

node::node(std::string name, volume* volume, node* parent) : name_(std::move(name)), volume_(volume), parent_(parent)
{
}

[[nodiscard]] node::node(const node& other) noexcept
    : name_(other.name_), volume_(other.volume_), parent_(other.parent_), subnodes_(other.subnodes_),
      values_(other.values_)
{
    for (auto& [name, subnode] : subnodes_)
    {
        subnode.parent_ = this;
    }
}

[[nodiscard]] node::node(node&& other) noexcept
    : name_(std::move(other.name_)), volume_(other.volume_), parent_(other.parent_),
      subnodes_(std::move(other.subnodes_)), values_(std::move(other.values_))
{
    for (auto& [name, subnode] : subnodes_)
    {
        subnode.parent_ = this;
    }
}

node& node::operator=(const node& rhs) noexcept
{
    if (this == &rhs)
        return *this;

    name_ = rhs.name_;
    volume_ = rhs.volume_;
    parent_ = rhs.parent_;
    subnodes_ = rhs.subnodes_;
    values_ = rhs.values_;

    for (auto& [name, subnode] : subnodes_)
    {
        subnode.parent_ = this;
    }

    return *this;
}

node& node::operator=(node&& rhs) noexcept
{
    name_ = std::move(rhs.name_);
    volume_ = rhs.volume_;
    parent_ = rhs.parent_;
    subnodes_ = std::move(rhs.subnodes_);
    values_ = std::move(rhs.values_);

    for (auto& [name, subnode] : subnodes_)
    {
        subnode.parent_ = this;
    }

    return *this;
}

node* node::create_subnode(path_view subnode_path)
{
    if (!subnode_path.valid())
        return nullptr;

    std::string subnode_name = std::string(*subnode_path.front());

    auto [it, inserted] = subnodes_.emplace(subnode_name, node(subnode_name, volume_, this));
    node* subnode = &it->second;

    subnode->deleted_ = false;

    if (subnode_path.composite())
    {
        subnode_path.pop_front();
        return subnode->create_subnode(std::move(subnode_path));
    }

    return subnode;
}

node* node::open_subnode(path_view subnode_path)
{
    if (!subnode_path.valid() || deleted_)
        return nullptr;

    const std::string subnode_name = std::string(*subnode_path.front());

    const auto it = subnodes_.find(subnode_name);
    if (it == subnodes_.end())
        return nullptr;

    node* subnode = &it->second;
    if (subnode->deleted_)
        return nullptr;

    if (subnode_path.composite())
    {
        subnode_path.pop_front();
        return subnode->open_subnode(std::move(subnode_path));
    }

    return subnode;
}

// size_t node::delete_subnode(path_view subnode_path)
// {
//     if (!subnode_path.valid())
//         return 0;
//
//     const std::string target_name = std::string(*subnode_path.back());
//     const node* subnode = open_subnode(std::move(subnode_path));
//     if (!subnode || !subnode->subnodes_.empty())
//         return 0;
//
//     return subnodes_.erase(target_name);
// }

size_t node::delete_subnode_tree(path_view subnode_path)
{
    if (!subnode_path.valid())
        return 0;

    node* target_subnode = open_subnode(std::move(subnode_path));
    if (!target_subnode)
        return 0;

    size_t num_deleted = 0;
    for (auto& [name, subnode] : target_subnode->subnodes_)
    {
        num_deleted += target_subnode->delete_subnode_tree(name);
    }

    num_deleted++;
    target_subnode->deleted_ = true;

    return num_deleted;
}

// void node::rename_subnode(const std::string& subnode_name, const std::string& new_subnode_name)
// {
//     if (subnodes_.find(new_subnode_name) != subnodes_.end())
//         return nullptr;
//
//     const auto it = subnodes_.find(subnode_name);
//     if (it == subnodes_.end())
//         return nullptr;
//     node* subnode = &it->second;
//     subnode->set_name(new_subnode_name);
//     subnodes_.emplace(new_subnode_name, std::move(*subnode));
//     subnodes_.erase(subnode_name);
// }

std::vector<std::string> node::get_subnode_names()
{
    // Copy strings to avoid scenarios when a subnode gets deleted
    // and the caller is left with a dangling pointer
    std::vector<std::string> names;
    names.reserve(subnodes_.size());
    for (const auto& [name, subnode] : subnodes_)
    {
        names.emplace_back(name);
    }
    return names;
}

size_t node::delete_value(const std::string& value_name)
{
    return values_.erase(value_name);
}

std::optional<value_kind> node::get_value_kind(const std::string& value_name) const
{
    const auto it = values_.find(value_name);
    if (it == values_.end())
        return std::nullopt;

    return static_cast<value_kind>(it->second.index());
}

std::unordered_set<std::string> node::get_value_names()
{
    // Copy strings to avoid scenarios when a value gets deleted
    // and the caller is left with a dangling pointer
    std::unordered_set<std::string> names;
    for (const auto& [name, value] : values_)
    {
        names.emplace(name);
    }
    return names;
}

std::string_view node::name()
{
    return name_;
}

void node::set_name(const std::string& new_subnode_name)
{
    name_ = new_subnode_name;
}

[[nodiscard]] uint8_t node::priority() const
{
    return volume_->priority();
}

[[nodiscard]] std::string node::path() const
{
    std::string path = name_;
    const node* parent = parent_;
    while (parent)
    {
        path = parent->name_ + path_view::path_separator + path;
        parent = parent->parent_;
    }
    return path;
}

void node::set_volume(volume* volume)
{
    volume_ = volume;
    for (auto& [name, subnode] : subnodes_)
    {
        subnode.set_volume(volume);
    }
}

std::ostream& operator<<(std::ostream& lhs, const node& rhs)
{
    lhs << rhs.path() << std::endl;

    for (const auto& [name, value] : rhs.values_)
    {
        lhs << name << " = " << value << std::endl;
    }

    for (const auto& [name, subnode] : rhs.subnodes_)
    {
        lhs << subnode;
    }

    return lhs;
}
}
