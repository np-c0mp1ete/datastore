#include "datastore/node.hpp"

#include "datastore/volume.hpp"

namespace datastore
{
node::node(const std::string& name, volume* volume, node* parent) : name_(name), volume_(volume), parent_(parent)
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

node* node::create_subnode(const std::string& subnode_path)
{
    const size_t dot_pos = subnode_path.find_first_of('.');
    std::string subnode_name = subnode_path.substr(0, dot_pos);

    auto [it, inserted] = subnodes_.emplace(subnode_name, node(subnode_name, volume_, this));
    node* subnode = &it->second;

    if (dot_pos != std::string::npos)
    {
        const std::string remaining_path = subnode_path.substr(dot_pos + 1);
        return subnode->create_subnode(remaining_path);
    }

    return subnode;
}

node* node::open_subnode(const std::string& subnode_path)
{
    const size_t dot_pos = subnode_path.find_first_of('.');
    std::string subnode_name = subnode_path.substr(0, dot_pos);

    const auto it = subnodes_.find(subnode_name);
    if (it == subnodes_.end())
        return nullptr;

    node* subnode = &it->second;
    if (dot_pos != std::string::npos)
    {
        const std::string remaining_path = subnode_path.substr(dot_pos + 1);
        return subnode->open_subnode(remaining_path);
    }

    return subnode;
}

size_t node::delete_subnode(const std::string& subnode_name)
{
    const node* subnode = open_subnode(subnode_name);
    if (subnode && subnode->subnodes_.empty())
    {
        return subnodes_.erase(subnode_name);
    }
    return 0;
}

void node::delete_subnode_tree(const std::string& subnode_name)
{
    subnodes_.erase(subnode_name);
}

void node::rename_subnode(const std::string& subnode_name, const std::string& new_subnode_name)
{
    // Check that new_subnode_name subnode doesn't exist
    node* new_subnode = open_subnode(new_subnode_name);
    if (new_subnode)
        return;

    node* subnode = open_subnode(subnode_name);
    if (subnode)
    {
        subnode->set_name(new_subnode_name);
        subnodes_.emplace(new_subnode_name, std::move(*subnode));
        delete_subnode_tree(subnode_name);
    }
}

std::vector<std::string_view> node::get_subnode_names()
{
    std::vector<std::string_view> names;
    names.reserve(subnodes_.size());
    for (const auto& [name, subnode] : subnodes_)
    {
        names.emplace_back(name);
    }
    return names;
}

void node::delete_value(const std::string& value_name)
{
    values_.erase(value_name);
}

value_kind node::get_value_kind(const std::string& value_name) const
{
    // TODO: at can throw
    return static_cast<value_kind>(values_.at(value_name).index());
}

std::vector<std::string_view> node::get_value_names()
{
    std::vector<std::string_view> names;
    names.reserve(values_.size());
    for (const auto& [name, value] : values_)
    {
        names.emplace_back(name);
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
    node* parent = parent_;
    while (parent)
    {
        path = parent->name_ + "." + path;
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
        value_kind kind = rhs.get_value_kind(name);
        lhs << name << " = " << *rhs.get_value<std::string>(name) << std::endl;
    }

    for (const auto& [name, subnode] : rhs.subnodes_)
    {
        lhs << subnode;
    }

    return lhs;
}
}
