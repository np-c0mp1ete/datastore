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

node_view::node_view(const path_view& full_path, size_t depth) : depth_(depth), nodes_(&detail::compare_nodes)
{
    if (!full_path.valid())
        return;

    full_path_ = full_path.str();
}

node_view::node_view(node_view&& other) noexcept
    : full_path_(std::move(other.full_path_)), depth_(other.depth_),
      subviews_(std::move(other.subviews_)), nodes_(std::move(other.nodes_)), expired_(other.expired_.load())
{
    nodes_.for_each([&](const std::shared_ptr<node>& node) {
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
    full_path_ = std::move(rhs.full_path_);
    depth_ = rhs.depth_;
    subviews_ = std::move(rhs.subviews_);
    nodes_ = std::move(rhs.nodes_);
    expired_ = rhs.expired_.load();

    nodes_.for_each([&](const std::shared_ptr<node>& node) {
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

    if (depth_ > vault::max_tree_depth)
        return nullptr;

    const std::string subnode_name = std::string(*subnode_path.front());

    // some subviews on the path might not have a node attached
    // root subview never has a node
    std::optional<std::shared_ptr<node_view>> opt = subviews_.find(subnode_name);
    if (opt)
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

    // on_create_subnode() won't create a subnode again in this case
    std::shared_ptr<node_view> subview(new node_view(subnode_path, depth_ + 1));
    const auto [real_subview, inserted] = subviews_.find_or_insert_with_limit(subnode_name, subview, max_num_subviews);
    if (!inserted)
        return nullptr;
    
    // real_subview->expired_ = false;
    //
    // real_subview->nodes_.push(subnode);

    return real_subview;
}

std::shared_ptr<node_view> node_view::open_subnode(path_view subview_path) const
{
    if (!subview_path.valid() || expired_)
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

    if (!subnode)
        return nullptr;

    if (subnode->deleted_)
        return nullptr;

    std::string target_subnode_name = subview_name.str();

    if (depth_ > vault::max_tree_depth)
        return nullptr;

    // Create a subview to hold the subnode
    std::shared_ptr<node_view> subview(new node_view(full_path_ + path_view::path_separator + target_subnode_name, depth_ + 1));
    const auto subview_inserted_pair =
        subviews_.find_or_insert_with_limit(target_subnode_name, subview, max_num_subviews);
    const auto [real_subview, inserted] = subview_inserted_pair;
    if (!inserted)
        return nullptr;

    // Try to load all subnodes recursively
    bool success = true;
    subnode->for_each_subnode([&](const std::shared_ptr<node>& sub) {
        success = success &&
                  subview_inserted_pair.first->load_subnode_tree(sub->name(), sub) != nullptr;
    });

    if (!success)
    {
        // Undo subview creation
        // TODO: add tests for this (and use cpp coverage to find similar code paths)
        subviews_.erase(target_subnode_name);
        return nullptr;
    }

    real_subview->nodes_.push(subnode);
    subnode->register_observer(real_subview.get());

    return real_subview;
}

bool node_view::unload_subnode_tree(path_view subview_name)
{
    if (!subview_name.valid() || subview_name.composite())
        return false;

    const std::shared_ptr<node_view>& subview = open_subnode(subview_name.str());
    if (!subview)
        return false;

    subview->expired_ = true;

    return subviews_.erase(subview_name.str()) > 0;
}

void node_view::unload_subnode_tree()
{
    subviews_.for_each([&](const std::shared_ptr<node_view>& subview) {
        subview->unload_subnode_tree();
        subview->expired_ = true;
    });

    subviews_.clear();
}

bool node_view::delete_subview_tree(path_view subview_name)
{
    if (!subview_name.valid() || subview_name.composite())
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
    size_t num_deleted = 0;

    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        num_deleted = node->delete_value(value_name);
        return num_deleted > 0;
    });

    return num_deleted;
}

void node_view::delete_values()
{
    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->delete_values();
    });
}

std::optional<value_kind> node_view::get_value_kind(const std::string& value_name) const
{
    std::optional<value_kind> kind;

    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        kind = node->get_value_kind(value_name);
        return kind.has_value();
    });

    return kind;
}

std::string_view node_view::name()
{
    if (expired_)
        return "";

    //TODO: unnecessary validation
    return path_view(full_path_).back().value();
}

[[nodiscard]] std::string node_view::path() const
{
    return full_path_;
}

std::ostream& operator<<(std::ostream& lhs, const node_view& rhs)
{
    lhs << rhs.path() << std::endl;

    rhs.nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->for_each_value([&](const attr& a) {
            lhs << "-> [" << static_cast<size_t>(node->priority()) << "] " << a.name() << "@" << node->path() << " = "
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
    std::shared_ptr<node_view> subview(new node_view(subnode->name(), depth_ + 1));
    const auto [real_subview, success] = subviews_.find_or_insert_with_limit(std::string(subnode->name()), subview, max_num_subviews);
    if (!success)
    {
        // Can be the case if too many subviews exist already
        return;
    }

    real_subview->nodes_.push(subnode);
}

void node_view::on_delete_subnode(const std::shared_ptr<node>& subnode)
{
    // TODO: it's incorrect to check node name, node_views might have a different name
    const std::string subnode_name = std::string(subnode->name());
    auto opt = subviews_.find(subnode_name);
    if (!opt)
        return;
    std::shared_ptr<node_view> subview = opt.value();

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
