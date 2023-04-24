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

node_view::node_view(const path_view& path, size_t depth) : depth_(depth), nodes_(&detail::compare_nodes)
{
    if (!path.valid())
        return;

    path_ = path.str();
}

//TODO: should node views be copyable?
// [[nodiscard]] node_view::node_view(const node_view& other) noexcept
//     : name_(other.name_), vault_(other.vault_), parent_(other.parent_), subviews_(other.subviews_),
//       nodes_(other.nodes_), observer_(this)
// {
//     for (auto& [name, subnode] : subviews_)
//     {
//         subnode.parent_ = this;
//     }
//
//     for (auto node : nodes_)
//     {
//         node->register_observer(&observer_);
//     }
// }

[[nodiscard]] node_view::node_view(node_view&& other) noexcept
    : path_(std::move(other.path_)), depth_(other.depth_),
      subviews_(std::move(other.subviews_)), nodes_(std::move(other.nodes_))
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

// node_view& node_view::operator=(const node_view& rhs) noexcept
// {
//     if (this == &rhs)
//         return *this;
//
//     name_ = rhs.name_;
//     vault_ = rhs.vault_;
//     parent_ = rhs.parent_;
//     subviews_ = rhs.subviews_;
//     nodes_ = rhs.nodes_;
//
//     for (auto& [name, subnode] : subviews_)
//     {
//         subnode.parent_ = this;
//     }
//
//     for (auto node : nodes_)
//     {
//         node->register_observer(&observer_);
//     }
//
//     return *this;
// }

node_view& node_view::operator=(node_view&& rhs) noexcept
{
    path_ = std::move(rhs.path_);
    depth_ = rhs.depth_;
    subviews_ = std::move(rhs.subviews_);
    nodes_ = std::move(rhs.nodes_);

    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        node->register_observer(this);
    });

    return *this;
}

std::shared_ptr<node_view> node_view::create_subnode(path_view subnode_path)
{
    if (!subnode_path.valid() || expired_)
        return nullptr;

    if (depth_ > vault::max_tree_depth)
        return nullptr;

    const std::string subnode_name = std::string(*subnode_path.front());

    // TODO: always takes the node with the highest precedence
    const std::shared_ptr<node>& subnode = (*nodes_.front())->create_subnode(std::move(subnode_path));
    if (!subnode)
        return nullptr;

    std::shared_ptr<node_view> subview(new node_view(subnode_path, depth_ + 1));
    const auto [real_subview, inserted] = subviews_.insert_with_limit_if_not_exist(subnode_name, subview, max_num_subnodes);
    if (!inserted)
        return nullptr;

    real_subview->expired_ = false;

    real_subview->nodes_.push(subnode);

    return real_subview;
}

std::shared_ptr<node_view> node_view::open_subnode(path_view subnode_path) const
{
    if (!subnode_path.valid() || expired_)
        return nullptr;

    const std::string subnode_name = std::string(*subnode_path.front());

    const auto opt = subviews_.find(subnode_name);
    if (!opt)
    {
        return nullptr;
    }

    std::shared_ptr<node_view> subnode = opt.value();

    if (subnode->expired_)
        return nullptr;

    if (subnode_path.composite())
    {
        subnode_path.pop_front();
        return subnode->open_subnode(std::move(subnode_path));
    }

    return subnode;
}

std::shared_ptr<node_view> node_view::load_subnode_tree(path_view subview_name, const std::shared_ptr<node>& subnode)
{
    if (!subview_name.valid() || subview_name.composite())
        return nullptr;

    std::string target_subnode_name = subview_name.str();

    if (depth_ > vault::max_tree_depth)
        return nullptr;

    // Create a subview to hold the subnode
    std::shared_ptr<node_view> subview(new node_view(path_ + path_view::path_separator + target_subnode_name, depth_ + 1));
    const auto [real_subview, inserted] =
        subviews_.insert_with_limit_if_not_exist(target_subnode_name, subview, max_num_subnodes);
    if (!inserted)
        return nullptr;

    // Try to load all subnodes recursively
    bool success = true;
    subnode->for_each_subnode([&](const std::pair<std::string, std::shared_ptr<node>>& kv_pair) {
        success = success && real_subview->load_subnode_tree(kv_pair.first, subnode->open_subnode(kv_pair.first)) != nullptr;
    });

    if (!success)
    {
        // Undo parent creation
        // TODO: add tests for this (and use cpp coverage to find similar code paths
        subviews_.erase(target_subnode_name);
        return nullptr;
    }

    real_subview->nodes_.push(subnode);
    subnode->register_observer(real_subview.get());

    return real_subview;
}

size_t node_view::unload_subnode_tree(path_view subview_name)
{
    if (!subview_name.valid() || subview_name.composite())
        return 0;

    const std::shared_ptr<node_view>& subview = open_subnode(subview_name.str());
    if (!subview)
        return 0;

    subview->expired_ = true;

    return subviews_.erase(subview_name.str());
}

size_t node_view::delete_subview_tree(path_view subview_name)
{
    if (!subview_name.valid() || subview_name.composite())
        return 0;

    const std::string target_subview_name = subview_name.str();

    //TODO: take care of the case when subnode_name != subview_name
    bool success = false;
    nodes_.for_each([&](const std::shared_ptr<node>& node) {
        success = success || node->delete_subnode_tree(target_subview_name);
    });

    subviews_.erase(target_subview_name);

    return success;
}

// std::unordered_set<std::string> node_view::get_subnode_names() const
// {
//     // Copy strings to avoid scenarios when a subnode gets deleted
//     // and the caller is left with a dangling pointer
//     std::unordered_set<std::string> names;
//
//
//     // for (const auto& [name, subnode] : subviews_)
//     // {
//     //     if (subnode.expired_)
//     //         continue;
//     //     names.insert(name);
//     // }
//     return names;
// }

size_t node_view::delete_value(const std::string& value_name)
{
    size_t num_deleted = 0;

    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        num_deleted = node->delete_value(value_name);
        return num_deleted > 0;
    });

    return num_deleted;
}

std::optional<value_kind> node_view::get_value_kind(const std::string& value_name) const
{
    std::optional<value_kind> kind;

    nodes_.find_first_if([&](const std::shared_ptr<node>& node) {
        return node->get_value_kind(value_name);
    });

    return kind;
}

std::string_view node_view::name()
{
    if (expired_)
        return "";

    //TODO: unnecessary validation
    return path_view(path_).back().value();
}

[[nodiscard]] std::string node_view::path() const
{
    return path_;
}

std::ostream& operator<<(std::ostream& lhs, const node_view& rhs)
{
    lhs << rhs.path() << std::endl;

    rhs.nodes_.for_each([&](const std::shared_ptr<node>& node) {
        auto values = node->values_.get_map();
        for (const auto& [name, value] : values)
        {
            lhs << "-> [" << static_cast<size_t>(node->priority()) << "] " << name << "@" << node->path() <<
                " = "
                << value << std::endl;
        }
    });

    rhs.subviews_.for_each([&](const std::pair<std::string, std::shared_ptr<node_view>>& kv_pair) {
        lhs << *kv_pair.second;
    });

    return lhs;
}

void node_view::on_create_subnode(const std::shared_ptr<node>& subnode)
{
    std::shared_ptr<node_view> subview(new node_view(subnode->name(), depth_ + 1));
    const auto [real_subview, inserted] = subviews_.insert_with_limit_if_not_exist(std::string(subnode->name()), subview, max_num_subnodes);
    if (!inserted)
        return;

    real_subview->expired_ = false;

    real_subview->nodes_.push(subnode);
}

void node_view::on_delete_subnode(const std::shared_ptr<node>& subnode)
{
    // TODO: it's incorrect to check node name, node_views might have a different name
    const std::string subnode_name = std::string(subnode->name());
    auto opt = subviews_.find(subnode_name);
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
