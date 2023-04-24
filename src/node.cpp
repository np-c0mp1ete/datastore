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

node::node(std::string name, std::string path, uint8_t volume_priority, size_t depth)
    : name_(std::move(name)), path_(std::move(path)), volume_priority(volume_priority), depth_(depth)
{
}

[[nodiscard]] node::node(node&& other) noexcept
    : name_(std::move(other.name_)), path_(std::move(other.path_)), volume_priority(other.volume_priority),
      subnodes_(std::move(other.subnodes_)),
      values_(std::move(other.values_)), depth_(other.depth_)
{
}

node& node::operator=(node&& rhs) noexcept
{
    name_ = std::move(rhs.name_);
    path_ = std::move(rhs.path_);
    volume_priority = rhs.volume_priority;
    depth_ = rhs.depth_;
    subnodes_ = std::move(rhs.subnodes_);
    values_ = std::move(rhs.values_);

    return *this;
}

std::shared_ptr<node> node::create_subnode(path_view subnode_path)
{
    if (!subnode_path.valid())
        return nullptr;

    if (depth_ >= volume::max_tree_depth)
        return nullptr;

    const std::string subnode_name = std::string(*subnode_path.front());

    std::shared_ptr<node> subnode(new node(subnode_name, path_ + path_view::path_separator + subnode_name, volume_priority, depth_ + 1));
    const auto& [real_subnode, success] = subnodes_.insert_with_limit_if_not_exist(subnode_name, subnode, max_num_subnodes);

    if (!success)
        return nullptr;

    real_subnode->deleted_ = false;

    if (subnode_path.composite())
    {
        subnode_path.pop_front();
        return real_subnode->create_subnode(std::move(subnode_path));
    }

    observers_.for_each([&](detail::node_observer* observer) { observer->on_create_subnode(real_subnode); });

    // for (std::weak_ptr<detail::node_observer>& observer : observers_)
    // {
    //     if (const std::shared_ptr<detail::node_observer>& valid_observer = observer.lock())
    //     {
    //         valid_observer->on_create_subnode(real_subnode);
    //     }
    // }

    return real_subnode;
}

std::shared_ptr<node> node::open_subnode(path_view subnode_path) const
{
    if (!subnode_path.valid() || deleted_)
        return nullptr;

    const std::string subnode_name = std::string(*subnode_path.front());

    const auto opt = subnodes_.find(subnode_name);
    if (!opt)
        return nullptr;

    auto subnode = *opt;

    if (subnode->deleted_)
        return nullptr;

    if (subnode_path.composite())
    {
        subnode_path.pop_front();
        return subnode->open_subnode(std::move(subnode_path));
    }

    return subnode;
}

void node::notify_on_delete_subnode_observers(const std::shared_ptr<node>& subnode) const
{
    subnode->for_each_subnode([&](const std::pair<std::string, std::shared_ptr<node>>& kv_pair) {
        subnode->notify_on_delete_subnode_observers(kv_pair.second);
    });

    observers_.for_each([&](detail::node_observer* observer) { observer->on_delete_subnode(subnode); });
}

bool node::delete_subnode_tree(path_view subnode_name)
{
    if (!subnode_name.valid() || subnode_name.composite())
        return false;

    const std::optional<std::shared_ptr<node>> opt = subnodes_.find(subnode_name.str());
    if (!opt)
        return false;
    const std::shared_ptr<node>& subnode = opt.value();

    const bool success = subnodes_.erase(subnode_name.str()) > 0;
    if (!success)
        return false;

    notify_on_delete_subnode_observers(subnode);

    

    // auto target_subnode = open_subnode(std::move(subnode_name));
    // if (!target_subnode)
    //     return 0;
    //
    // size_t num_deleted = 0;
    // for (auto& [name, subnode] : target_subnode->subnodes_)
    // {
    //     num_deleted += target_subnode->delete_subnode_tree(name);
    // }
    //
    // num_deleted++;
    // target_subnode->deleted_ = true;
    //
    // for (auto observer : target_subnode->parent_->observers_)
    // {
    //     observer->on_delete_subnode(target_subnode.get());
    // }

    return success;
}

// std::unordered_set<std::string> node::get_subnode_names()
// {
//     // Copy strings to avoid scenarios when a subnode gets deleted
//     // and the caller is left with a dangling pointer
//     std::unordered_set<std::string> names;
//     // names.reserve(subnodes_.size());
//     // for (const auto& [name, subnode] : subnodes_)
//     // {
//     //     names.emplace(name);
//     // }
//     return names;
// }

size_t node::delete_value(const std::string& value_name)
{
    return values_.erase(value_name);
}

std::optional<value_kind> node::get_value_kind(const std::string& value_name) const
{
    const auto opt_value = values_.find(value_name);

    if (!opt_value)
        return std::nullopt;

    return static_cast<value_kind>(opt_value->index());
}

std::string_view node::name()
{
    return name_;
}

// void node::set_name(const std::string& new_subnode_name)
// {
//     name_ = new_subnode_name;
// }

[[nodiscard]] uint8_t node::priority() const
{
    return volume_priority;
}

[[nodiscard]] std::string node::path() const
{
    return path_;
}

// void node::set_volume(volume* volume)
// {
//     volume_priority = volume;
//     for (auto& [name, subnode] : subnodes_)
//     {
//         subnode.set_volume(volume);
//     }
// }

void node::register_observer(detail::node_observer* observer)
{
    observers_.push(observer);
}

void node::unregister_observer(const detail::node_observer* observer)
{
    observers_.remove_if([&](const detail::node_observer* element) { return element == observer; });
}

std::ostream& operator<<(std::ostream& lhs, const node& rhs)
{
    lhs << rhs.path() << std::endl;

    auto values = rhs.values_.get_map();
    for (const auto& [name, value] : values)
    {
        lhs << name << " = " << value << std::endl;
    }

    rhs.for_each_subnode([&](const std::pair<std::string, std::shared_ptr<node>>& kv_pair) {
        lhs << *kv_pair.second;
    });

    // for (const auto& [name, subnode] : rhs.subnodes_)
    // {
    //     lhs << subnode;
    // }

    return lhs;
}
}
