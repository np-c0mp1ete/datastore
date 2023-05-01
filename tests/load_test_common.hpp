#pragma once

#include "datastore/vault.hpp"
#include "datastore/volume.hpp"

#include <array>

namespace load_test
{
template <size_t... Ints>
auto to_string(std::integer_sequence<size_t, Ints...>)
{
    return std::array<std::string, sizeof...(Ints)>{{std::to_string(Ints)...}};
}

// Cache stringified indices
constexpr size_t max_node_idx = std::max(datastore::node::max_num_subnodes, datastore::node::max_num_values);
constexpr size_t max_node_view_idx =
    std::max(datastore::node_view::max_num_subviews, datastore::node_view::max_num_values);
constexpr size_t max_idx = std::max(max_node_idx, max_node_view_idx);
const auto idx_str = to_string(std::make_index_sequence<max_idx>{});

const std::string max_str = std::string(datastore::max_str_value_size_bytes, 'a');

inline datastore::volume vol1(datastore::volume::priority_class::medium);
inline datastore::volume vol2(datastore::volume::priority_class::medium);

inline void node_create_tree(const std::shared_ptr<datastore::node>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= datastore::volume::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node::max_num_subnodes; subnode_idx++)
    {
        std::shared_ptr<datastore::node> subnode = parent->create_subnode(idx_str[subnode_idx]);
        node_create_tree(subnode, cur_depth + 1);
    }

    for (size_t value_idx = 0; value_idx < datastore::node::max_num_values; value_idx++)
    {
        parent->set_value(idx_str[value_idx], max_str);
    }
}

inline void node_view_create_tree(const std::shared_ptr<datastore::node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= datastore::vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node_view::max_num_subviews; subnode_idx++)
    {
        std::shared_ptr<datastore::node_view> subnode = parent->create_subnode(idx_str[subnode_idx]);

        // Can happen if e.g. the parent was deleted from the vault
        if (!subnode)
            continue;

        node_view_create_tree(subnode, cur_depth + 1);
    }

    for (size_t value_idx = 0; value_idx < datastore::node_view::max_num_values; value_idx++)
    {
        parent->set_value(idx_str[value_idx], max_str);
    }
}

inline void node_get_tree(const std::shared_ptr<datastore::node>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= datastore::volume::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node::max_num_subnodes; subnode_idx++)
    {
        const std::shared_ptr<datastore::node> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_get_tree(subnode, cur_depth + 1);
    }

    for (size_t value_idx = 0; value_idx < datastore::node::max_num_values; value_idx++)
    {
        DATASTORE_UNUSED(parent->get_value_kind(idx_str[value_idx]));
        DATASTORE_UNUSED(parent->get_value<std::string>(idx_str[value_idx]));
    }

    parent->for_each_subnode([&](const std::shared_ptr<datastore::node>& subnode) {
        // Make sure it's safe to work with values while holding a subnodes lock
        parent->for_each_value([&](const datastore::attr& a) {
            DATASTORE_UNUSED(*a.get_value_kind());
            DATASTORE_UNUSED(*a.get_value<std::string>());
        });

        node_get_tree(subnode, cur_depth + 1);
    });

    parent->for_each_value([&](const datastore::attr& a) {
        DATASTORE_UNUSED(*a.get_value_kind());
        DATASTORE_UNUSED(*a.get_value<std::string>());
    });
}

inline void node_view_get_tree(const std::shared_ptr<datastore::node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= datastore::vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node_view::max_num_subviews; subnode_idx++)
    {
        const std::shared_ptr<datastore::node_view> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_view_get_tree(subnode, cur_depth + 1);
    }

    for (size_t value_idx = 0; value_idx < datastore::node_view::max_num_values; value_idx++)
    {
        DATASTORE_UNUSED(parent->get_value_kind(idx_str[value_idx]));
        DATASTORE_UNUSED(parent->get_value<std::string>(idx_str[value_idx]));
    }

    parent->for_each_subnode([&](const std::shared_ptr<datastore::node_view>& subnode) {
        // Due to a potential deadlock caused by a cycle in a lock order graph,
        // it's NOT safe to work with values while holding a subviews lock
        // Run with thread sanitizer for details.

        // parent->for_each_value([&](const attr& a) {
        //     DATASTORE_UNUSED(*a.get_value_kind());
        //     DATASTORE_UNUSED(*a.get_value<std::string>());
        // });

        node_view_get_tree(subnode, cur_depth + 1);
    });

    parent->for_each_value([&](const datastore::attr& a) {
        DATASTORE_UNUSED(*a.get_value_kind());
        DATASTORE_UNUSED(*a.get_value<std::string>());
    });
}

inline void node_delete_tree(const std::shared_ptr<datastore::node>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= datastore::volume::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node::max_num_subnodes; subnode_idx++)
    {
        const std::shared_ptr<datastore::node> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_delete_tree(subnode, cur_depth + 1);

        parent->delete_subnode_tree(idx_str[subnode_idx]);
    }

    // Test deleting attributes one by one
    for (size_t value_idx = 0; value_idx < datastore::node::max_num_values; value_idx++)
    {
        parent->delete_value(idx_str[value_idx]);
    }

    parent->for_each_subnode([&](const std::shared_ptr<datastore::node>& subnode) {
        node_delete_tree(subnode, cur_depth + 1);

        subnode->delete_values();
    });

    parent->delete_subnode_tree();
    parent->delete_values();
}

inline void node_view_delete_tree(const std::shared_ptr<datastore::node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= datastore::vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node_view::max_num_subviews; subnode_idx++)
    {
        const std::shared_ptr<datastore::node_view> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_view_delete_tree(subnode, cur_depth + 1);

        parent->delete_subview_tree(idx_str[subnode_idx]);
    }

    // Test deleting attributes one by one
    for (size_t value_idx = 0; value_idx < datastore::node_view::max_num_values; value_idx++)
    {
        parent->delete_value(idx_str[value_idx]);
    }

    parent->for_each_subnode([&](const std::shared_ptr<datastore::node_view>& subnode) {
        node_view_delete_tree(subnode, cur_depth + 1);

        subnode->delete_values();
    });

    parent->delete_subview_tree();
    parent->delete_values();
}

inline void node_view_load_subnode(const std::shared_ptr<datastore::node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= datastore::vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node_view::max_num_subviews; subnode_idx++)
    {
        const std::shared_ptr<datastore::node_view> subnode =
            parent->load_subnode_tree(idx_str[subnode_idx], vol1.root());
        parent->load_subnode_tree(idx_str[subnode_idx], vol2.root());
        if (!subnode)
            continue;
        node_view_load_subnode(subnode, cur_depth + 1);
    }
}

inline void node_view_unload_subnode(const std::shared_ptr<datastore::node_view>& parent, size_t cur_depth = 1)
{
    if (cur_depth >= datastore::vault::max_tree_depth)
        return;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node_view::max_num_subviews; subnode_idx++)
    {
        const std::shared_ptr<datastore::node_view> subnode = parent->open_subnode(idx_str[subnode_idx]);
        if (!subnode)
            continue;
        node_view_unload_subnode(subnode, cur_depth + 1);

        parent->unload_subnode_tree(idx_str[subnode_idx]);
    }

    parent->unload_subnode_tree();
}

inline void volume_save(const std::shared_ptr<datastore::node>&, size_t) {
    vol1.save("vol1.vol");
    vol2.save("vol2.vol");
};
} // namespace load_test
