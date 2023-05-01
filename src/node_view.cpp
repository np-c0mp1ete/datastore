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
    : full_path_str_(full_path.str()), full_path_view_(full_path_str_), nodes_(&detail::compare_nodes)
{
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
    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->unregister_observer(&other);
        node->register_observer(this);
    });
}

node_view::~node_view() noexcept
{
    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->unregister_observer(this);
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
    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->unregister_observer(&rhs);
        node->register_observer(this);
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

    const std::string subnode_name = std::string(*subnode_path.front());

    // some subviews on the path might not have a node attached
    // root subview never has a node
    if (std::optional<std::shared_ptr<node_view>> opt = subviews_.find(subnode_name))
    {
        if (!subnode_path.composite())
            return opt.value();

        subnode_path.pop_front();
        return opt.value()->create_subnode(std::move(subnode_path));
    }

    // always takes the node with the highest precedence
    const auto& main_node = nodes_.front();
    if (!main_node)
        return nullptr;

    const std::shared_ptr<node>& subnode = (*main_node)->create_subnode(std::move(subnode_path));
    if (!subnode)
        return nullptr;

    const auto [subview, success] = subviews_.find_or_insert_with_limit(
        subnode_name, new node_view(full_path_view_ + subnode_name), max_num_subviews);
    if (!success)
        return nullptr;

    subview->nodes_.push(subnode);

    return subview;
}

std::shared_ptr<node_view> node_view::open_subnode(path_view subview_path) const
{
    if (!subview_path.valid())
        return nullptr;

    if (expired_)
        return nullptr;

    const std::string subview_name = std::string(*subview_path.front());

    const auto opt = subviews_.find(subview_name);
    if (!opt)
    {
        return nullptr;
    }

    std::shared_ptr<node_view> subview = opt.value();

    if (subview->expired_)
        return nullptr;

    if (subview_path.composite())
    {
        subview_path.pop_front();
        return subview->open_subnode(std::move(subview_path));
    }

    return subview;
}

std::shared_ptr<node_view> node_view::load_subnode_tree(path_view subview_name, const std::shared_ptr<node>& subnode)
{
    if (!subview_name.valid() || subview_name.composite())
        return nullptr;

    if (expired_)
        return nullptr;

    if (!subnode)
        return nullptr;

    if (subnode->deleted_)
        return nullptr;

    if (full_path_view_.size() >= vault::max_tree_depth)
        return nullptr;

    std::string name = subview_name.str();

    // Create a subview to hold the subnode
    const auto& subview_inserted_pair = subviews_.find_or_insert_with_limit(
        name, new node_view(full_path_view_ + name), max_num_subviews);
    const auto& [subview, success] = subview_inserted_pair;
    if (!success)
        return nullptr;

    // Try to load all subnodes recursively
    bool subnodes_loaded = true;
    subnode->for_each_subnode([&](const std::shared_ptr<node>& sub) {
        subnodes_loaded = subnodes_loaded &&
                  subview_inserted_pair.first->load_subnode_tree(sub->name(), sub) != nullptr;
    });

    if (!subnodes_loaded)
    {
        // Undo subview creation
        // TODO: add tests for this (and use cpp coverage to find similar code paths)
        DATASTORE_ASSERT(false);
        subviews_.erase(name);
        return nullptr;
    }

    subview->nodes_.push(subnode);
    subnode->register_observer(subview.get());

    return subview;
}

bool node_view::unload_subnode_tree(path_view subview_name)
{
    if (!subview_name.valid() || subview_name.composite())
        return false;

    if (expired_)
        return false;

    const std::shared_ptr<node_view>& subview = open_subnode(subview_name);
    if (!subview)
        return false;

    subview->unload_subnode_tree();

    subview->expired_ = true;
    subview->nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->unregister_observer(subview.get());
    });

    return subviews_.erase(subview_name.str()) > 0;
}

void node_view::unload_subnode_tree()
{
    if (expired_)
        return;

    subviews_.for_each([&](const std::shared_ptr<node_view>& subview) {
        subview->unload_subnode_tree();

        subview->expired_ = true;
        subview->nodes_.for_each([&](const std::shared_ptr<node>& node) {
            node->unregister_observer(subview.get());
        });
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

    //TODO: take care of the case when subnode_name != subview_name
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

    // TODO: take care of the case when subnode_name != subview_name
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

    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->delete_values();
    });
}

std::optional<value_kind> node_view::get_value_kind(const std::string& value_name) const
{
    if (expired_)
        return std::nullopt;

    std::optional<value_kind> kind;

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
            lhs << "-> [" << static_cast<size_t>(node->priority()) << "] " << a.name() << "@" << node->path().str() << " = "
                << a.value() << std::endl;
        });
    });

    rhs.subviews_.for_each([&](const std::shared_ptr<node_view>& subview) {
        lhs << *subview;
    });

    return lhs;
}

void node_view::on_create_subnode(const std::shared_ptr<node>& subnode)
{
    if (expired_)
        return;

    std::string subnode_name = std::string(subnode->name());

    const auto [subview, success] =
        subviews_.find_or_insert_with_limit(subnode_name, new node_view(full_path_view_ + subnode_name), max_num_subviews);
    if (!success)
    {
        // Too many subviews exist already
        return;
    }

    subview->nodes_.push(subnode);
}

void node_view::on_delete_subnode(const std::shared_ptr<node>& subnode)
{
    if (expired_)
        return;

    // TODO: it's incorrect to check node name, node_views might have a different name
    const std::string subnode_name = std::string(subnode->name());
    const auto opt = subviews_.find(subnode_name);
    if (!opt)
        return;
    const std::shared_ptr<node_view>& subview = opt.value();

    subview->nodes_.remove_if([&](const std::shared_ptr<node>& node) {
        return node == subnode;
    });

    if (subview->nodes_.size() == 0)
    {
        subview->expired_ = true;
        subviews_.erase(subnode_name);
    }
}
} // namespace datastore
