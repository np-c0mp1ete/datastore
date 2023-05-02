#include "datastore/node_view.hpp"

#include "datastore/vault.hpp"

namespace datastore
{
namespace detail
{
bool compare_nodes(const std::shared_ptr<node>& n1, const std::shared_ptr<node>& n2)
{
    if (n1->priority() != n2->priority())
        return n1->priority() > n2->priority();

    return n1 > n2;
}

} // namespace detail

node_view::node_view(path_view full_path)
    : full_path_str_(full_path.str()),
      full_path_view_(full_path_str_),
      nodes_(&detail::compare_nodes)
{
    // Play dead if somehow the path was invalid
    if (!full_path_view_.valid())
        expired_ = true;
}

node_view::node_view(node_view&& other) noexcept
    : full_path_str_(std::move(other.full_path_str_)),
      full_path_view_(full_path_str_),
      subviews_(std::move(other.subviews_)),
      nodes_(std::move(other.nodes_)),
      expired_(other.expired_.load())
{
    // Iterate over newly acquired nodes and update their observers lists
    // TODO: operating on subviews doesn't make much sense and needs to be reverted back
    // Likely I'd be better off inhereting from std::enable_shared_from_this
    subviews_.for_each([&](const std::shared_ptr<node_view>& subview) {
        subview->nodes_.for_each([&](const std::shared_ptr<node>& node) {
            node->register_observer(std::static_pointer_cast<node_observer>(subview));
        });
    });
}

node_view& node_view::operator=(node_view&& rhs) noexcept
{
    full_path_str_ = std::move(rhs.full_path_str_);
    full_path_view_ = path_view(full_path_str_);
    subviews_ = std::move(rhs.subviews_);
    nodes_ = std::move(rhs.nodes_);
    expired_ = rhs.expired_.load();

    // Iterate over newly acquired nodes and update their observers lists
    // TODO: operating on subviews doesn't make much sense and needs to be reverted back
    // Likely I'd be better off inhereting from std::enable_shared_from_this
    subviews_.for_each([&](const std::shared_ptr<node_view>& subview) {
        subview->nodes_.for_each([&](const std::shared_ptr<node>& node) {
            node->register_observer(std::static_pointer_cast<node_observer>(subview));
        });
    });

    return *this;
}

std::shared_ptr<node_view> node_view::create_subnode(path_view subnode_path)
{
    if (!subnode_path.valid())
        return nullptr;

    if (expired_)
        return nullptr;

    // Can't go any deeper
    if (full_path_view_.size() >= vault::max_tree_depth)
        return nullptr;

    // Take the first element of the path
    const std::string subnode_name = std::string(*subnode_path.front());

    // root subview never has a node loaded
    if (std::optional<std::shared_ptr<node_view>> opt = subviews_.find(subnode_name))
    {
        if (!subnode_path.composite())
            return opt.value();

        subnode_path.pop_front();
        return opt.value()->create_subnode(std::move(subnode_path));
    }

    // Always takes an observed node with the highest priority as a parent for a new subnode
    const auto& main_node = nodes_.front();
    if (!main_node)
        return nullptr;

    const std::shared_ptr<node>& subnode = (*main_node)->create_subnode(std::move(subnode_path));
    if (!subnode)
        return nullptr;

    // TODO: subnode is actually the deepest subnode on the given path, so the code below is wrong
    const auto [subview, success] = subviews_.find_or_insert_with_limit(
        subnode_name, new node_view(full_path_view_ + subnode_name), max_num_subviews);
    if (!success)
        return nullptr;

    // TODO: node::create_subnode() might have just opened an already existing subnode
    // So starting observing this subnode again in this case is incorrect
    subview->nodes_.push(subnode);
    subnode->register_observer(subview);

    return subview;
}

std::shared_ptr<node_view> node_view::open_subnode(path_view subview_path) const
{
    if (!subview_path.valid())
        return nullptr;

    if (expired_)
        return nullptr;

    // Take the first element of the given path
    const std::string subview_name = std::string(*subview_path.front());

    const auto& opt = subviews_.find(subview_name);
    if (!opt)
    {
        return nullptr;
    }

    const std::shared_ptr<node_view>& subview = opt.value();

    // Recursively open subnodes if the path is composite
    if (subview_path.composite())
    {
        subview_path.pop_front();
        return subview->open_subnode(std::move(subview_path));
    }

    return subview;
}

std::shared_ptr<node_view> node_view::load_subnode_tree(const std::shared_ptr<node>& subnode)
{
    if (expired_)
        return nullptr;

    if (!subnode)
        return nullptr;

    if (subnode->deleted_)
        return nullptr;

    // Maximum vault hierarchy depth is already reached, can't load a subnode
    if (full_path_view_.size() >= vault::max_tree_depth)
        return nullptr;

    std::string name = std::string(subnode->name());

    // Create a subview to hold the subnode
    const auto& subview_success_pair =
        subviews_.find_or_insert_with_limit(name, new node_view(full_path_view_ + name), max_num_subviews);
    const auto& [subview, success] = subview_success_pair;
    if (!success)
        return nullptr;

    // Try to load all subnodes of a given subnode recursively
    bool subnodes_loaded = true;
    subnode->for_each_subnode([&](const std::shared_ptr<node>& sub) {
        subnodes_loaded = subnodes_loaded && subview_success_pair.first->load_subnode_tree(sub) != nullptr;
    });

    if (!subnodes_loaded)
    {
        // Undo subviews creation
        unload_subnode_tree(name);
        return nullptr;
    }

    // TODO: in case the user calls this function twice with the same node
    // Node subscription will be performed twice
    subview->nodes_.push(subnode);
    subnode->register_observer(subview);

    return subview;
}

bool node_view::unload_subnode_tree(path_view subview_name)
{
    if (!subview_name.valid() || subview_name.composite())
        return false;

    if (expired_)
        return false;

    // Find a subview with the given name
    const std::shared_ptr<node_view>& subview = open_subnode(subview_name);
    if (!subview)
        return false;

    // Unload all subviews of a given subview
    subview->unload_subnode_tree();

    // Mark the node view as expired for all the clients that might have a reference to it
    subview->expired_ = true;

    // Make the subview stop observing any nodes
    subview->nodes_.remove_if([](const std::shared_ptr<node>&) {
        return true;
    });

    return subviews_.erase(subview_name.str()) > 0;
}

void node_view::unload_subnode_tree()
{
    if (expired_)
        return;

    subviews_.for_each([&](const std::shared_ptr<node_view>& subview) {
        subview->unload_subnode_tree();

        // Make the subview stop observing any nodes
        subview->nodes_.remove_if([](const std::shared_ptr<node>&) {
            return true;
        });

        subview->expired_ = true;
    });

    subviews_.clear();
}

bool node_view::delete_subview_tree(path_view subview_name)
{
    if (!subview_name.valid() || subview_name.composite())
        return false;

    if (expired_)
        return false;

    const std::string target_subview_name = subview_name.str();

    bool success = false;
    // subview will be deleted in the on_delete_subnode() callback
    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        success = success || node->delete_subnode_tree(target_subview_name);
    });

    return success;
}

bool node_view::delete_subview_tree()
{
    if (expired_)
        return false;

    bool success = false;
    // each subview will be deleted in the on_delete_subnode() callback
    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        success = success || node->delete_subnode_tree();
    });

    return success;
}

size_t node_view::delete_value(const std::string& value_name)
{
    if (expired_)
        return 0;

    size_t num_deleted = 0;

    // Iterate over observed nodes until we find one which has an attribute with the given name
    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        num_deleted = node->delete_value(value_name);
        return num_deleted > 0;
    });

    return num_deleted;
}

void node_view::delete_values()
{
    if (expired_)
        return;

    // Delete all values from all observed nodes
    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->delete_values();
    });
}

std::optional<value_kind> node_view::get_value_kind(const std::string& value_name) const
{
    if (expired_)
        return std::nullopt;

    std::optional<value_kind> kind;

    // Iterate over observed nodes until we find one which has an attribute with the given name
    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        kind = node->get_value_kind(value_name);
        return kind.has_value();
    });

    return kind;
}

std::string_view node_view::name() const
{
    return *full_path_view_.back();
}

path_view node_view::path() const
{
    return full_path_view_;
}

bool node_view::expired() const
{
    return expired_;
}

std::ostream& operator<<(std::ostream& lhs, const node_view& rhs)
{
    lhs << rhs.path().str();

    if (rhs.expired())
        lhs << " (expired)";
    lhs << std::endl;

    rhs.nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->for_each_value([&](const attr& a) {
            lhs << "-> [" << static_cast<size_t>(node->priority()) << "] " << a.name() << "@" << node->path().str()
                << " = " << a.value() << std::endl;
        });
    });

    rhs.subviews_.for_each([&](const std::shared_ptr<node_view>& subview) {
        lhs << *subview;
    });

    return lhs;
}

// Called when an observed node creates a new subnode
// TODO: currently it's also called if an existing subnode was opened using node::create_subnode()
void node_view::on_create_subnode(const std::shared_ptr<node>& subnode)
{
    if (expired_)
        return;

    std::string subnode_name = std::string(subnode->name());

    const auto [subview, success] = subviews_.find_or_insert_with_limit(
        subnode_name, new node_view(full_path_view_ + subnode_name), max_num_subviews);
    if (!success)
    {
        // Too many subviews exist already
        return;
    }

    // Make the subview start observing the subnode and subscribe to notifications from it
    subview->nodes_.push(subnode);
    subnode->register_observer(subview);
}

// Called when a subnode of an observed node was deleted
void node_view::on_delete_subnode(const std::shared_ptr<node>& subnode)
{
    // TODO: is it really possible that a node view has expired even though it is observing valid nodes?
    if (expired_)
        return;

    const std::string subnode_name = std::string(subnode->name());

    // Find a subview that observes the deleted subnode
    const auto& opt = subviews_.find(subnode_name);
    if (!opt)
        return;
    const std::shared_ptr<node_view>& subview = opt.value();

    // Make the subview stop observing the deleted subnode
    subview->nodes_.remove_if([&](const std::shared_ptr<node>& node) {
        return node == subnode;
    });

    // If there are no other nodes that the subview observes,
    // Mark it as expired and delete from the subview list
    // TODO: should likely delete the subview using unload_subview_tree
    // To also mark the entire subview hierarchy as expired
    if (subview->nodes_.size() == 0)
    {
        subview->expired_ = true;
        subviews_.erase(subnode_name);
    }
}
} // namespace datastore
