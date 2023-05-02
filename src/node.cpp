#include "datastore/node.hpp"
#include "datastore/volume.hpp"

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
    else
        lhs << "<unknown_type>";

    return lhs;
}

node::node(path_view full_path, uint8_t volume_priority)
    : full_path_str_(full_path.str()),
      full_path_view_(full_path_str_),
      volume_priority(volume_priority)
{
    // Play dead if the path is invalid
    if (!full_path_view_.valid())
        deleted_ = true;
}

node::node(node&& other) noexcept
    : full_path_str_(std::move(other.full_path_str_)),
      full_path_view_(full_path_str_),
      volume_priority(other.volume_priority),
      subnodes_(std::move(other.subnodes_)),
      values_(std::move(other.values_)),
      observers_(std::move(other.observers_)),
      deleted_(other.deleted_.load())
{
}

node& node::operator=(node&& rhs) noexcept
{
    full_path_str_ = std::move(rhs.full_path_str_);
    full_path_view_ = path_view(full_path_str_);
    volume_priority = rhs.volume_priority;
    subnodes_ = std::move(rhs.subnodes_);
    values_ = std::move(rhs.values_);
    observers_ = std::move(rhs.observers_);
    deleted_ = rhs.deleted_.load();

    return *this;
}

std::shared_ptr<node> node::create_subnode(path_view subnode_path)
{
    if (!subnode_path.valid())
        return nullptr;

    if (deleted_)
        return nullptr;

    if (full_path_view_.size() >= volume::max_tree_depth)
        return nullptr;

    // Take the first element of the given path
    const std::string subnode_name = std::string(*subnode_path.front());

    // Try to find an existing subnode or create a new one if the limit of subnodes is not reached
    const auto& subnode_success_pair = subnodes_.find_or_insert_with_limit(
        subnode_name, new node(full_path_view_ + subnode_name, volume_priority), max_num_subnodes);
    const auto& [subnode, success] = subnode_success_pair;
    if (!success)
        return nullptr;

    // Recursively create subnodes if a composite path was specified
    if (subnode_path.composite())
    {
        subnode_path.pop_front();
        return subnode->create_subnode(std::move(subnode_path));
    }

    // Cleanup expired observers
    observers_.remove_if([](const std::weak_ptr<detail::node_observer>& observer) {
        return observer.expired();
    });

    // Notify existing observers about subnode creation
    // TODO: don't notify if it was just an opening of an existing subnode
    observers_.for_each([&](const std::weak_ptr<detail::node_observer>& observer) {
        if (const std::shared_ptr<detail::node_observer>& valid_observer = observer.lock())
        {
            valid_observer->on_create_subnode(subnode_success_pair.first);
        }
    });

    return subnode;
}

std::shared_ptr<node> node::open_subnode(path_view subnode_path) const
{
    if (!subnode_path.valid())
        return nullptr;

    if (deleted_)
        return nullptr;

    // Take the first element of the given path and try to find a subnode with this name
    const std::string subnode_name = std::string(*subnode_path.front());

    const auto& opt = subnodes_.find(subnode_name);
    if (!opt)
        return nullptr;

    const std::shared_ptr<node>& subnode = *opt;

    // Open subnodes recursively if the path has multiple elements
    if (subnode_path.composite())
    {
        subnode_path.pop_front();
        return subnode->open_subnode(std::move(subnode_path));
    }

    return subnode;
}

void node::notify_on_delete_subnode_observers(const std::shared_ptr<node>& subnode)
{
    // Go bottom up the tree
    subnode->for_each_subnode([&](const std::shared_ptr<node>& node) {
        subnode->notify_on_delete_subnode_observers(node);
    });

    // Cleanup observers list
    observers_.remove_if([](const std::weak_ptr<detail::node_observer>& observer) {
        return observer.expired();
    });

    // Let existing observers know about the subnode deletion
    observers_.for_each([&](const std::weak_ptr<detail::node_observer>& observer) {
        if (const std::shared_ptr<detail::node_observer>& valid_observer = observer.lock())
        {
            valid_observer->on_delete_subnode(subnode);
        }
    });

    // Finally mark the subnode as deleted
    subnode->deleted_ = true;

    // TODO: subnodes don't erase children recursively
}

bool node::delete_subnode_tree(path_view subnode_name)
{
    if (!subnode_name.valid() || subnode_name.composite())
        return false;

    if (deleted_)
        return false;

    const std::optional<std::shared_ptr<node>> opt = subnodes_.find(subnode_name.str());
    if (!opt)
        return false;
    const std::shared_ptr<node>& subnode = opt.value();

    notify_on_delete_subnode_observers(subnode);

    const bool success = subnodes_.erase(subnode_name.str()) > 0;
    if (!success)
        return false;

    return success;
}

bool node::delete_subnode_tree()
{
    if (deleted_)
        return false;

    // notify_on_delete_subnode_observers() doesn't take subnodes_ locks internally
    // So it's safe to call it already holding a lock in for_each()
    subnodes_.for_each([&](const std::shared_ptr<node>& subnode) {
        // Entire observers hierarchy needs to be notified about the subnode tree deletion first
        notify_on_delete_subnode_observers(subnode);
    });

    subnodes_.clear();

    return true;
}

size_t node::delete_value(const std::string& value_name)
{
    if (deleted_)
        return 0;

    return values_.erase(value_name);
}

void node::delete_values()
{
    if (deleted_)
        return;

    values_.clear();
}

std::optional<value_kind> node::get_value_kind(const std::string& value_name) const
{
    if (deleted_)
        return std::nullopt;

    const auto& opt_value = values_.find(value_name);

    if (!opt_value)
        return std::nullopt;

    return opt_value->get_value_kind();
}

std::string_view node::name() const
{
    return *full_path_view_.back();
}

path_view node::path() const
{
    return full_path_view_;
}

uint8_t node::priority() const
{
    return volume_priority;
}

bool node::deleted() const
{
    return deleted_;
}

void node::register_observer(const std::shared_ptr<detail::node_observer>& observer)
{
    if (deleted_)
        return;

    observers_.push(observer);
}

std::ostream& operator<<(std::ostream& lhs, const node& rhs)
{
    lhs << rhs.path().str();

    if (rhs.deleted())
        lhs << " (deleted)";
    lhs << std::endl;

    rhs.for_each_value([&](const attr& a) {
        lhs << a.name() << " = " << a.value() << std::endl;
    });

    rhs.for_each_subnode([&](const std::shared_ptr<node>& subnode) {
        lhs << *subnode;
    });

    return lhs;
}
} // namespace datastore
