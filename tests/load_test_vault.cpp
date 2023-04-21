#include <catch2/catch_test_macros.hpp>

#include "datastore/vault.hpp"
#include "datastore/volume.hpp"

#include <array>

namespace
{
template <size_t... Ints>
auto to_string(std::integer_sequence<size_t, Ints...>)
{
    return std::array<std::string, sizeof...(Ints)>{{std::to_string(Ints)...}};
}

// Cache stringified indices
constexpr size_t max_idx =
    std::max(datastore::node_view::max_num_subnodes, datastore::node_view::max_num_values);
const auto idx_str = to_string(std::make_index_sequence<max_idx>{});

const std::string max_str = std::string(datastore::max_str_value_size_bytes, 'a');

void init_tree(datastore::node_view* parent, size_t cur_depth = 0)
{
    if (cur_depth > datastore::vault::max_tree_depth)
        return;
    cur_depth++;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node_view::max_num_subnodes; subnode_idx++)
    {
        datastore::node_view* subnode = parent->create_subnode(idx_str[subnode_idx]);
        CHECK(subnode != nullptr);

        for (size_t value_idx = 0; value_idx < datastore::node_view::max_num_values; value_idx++)
        {
            // string copy is the main bottleneck
            CHECK(subnode->set_value(idx_str[value_idx], max_str));
        }

        init_tree(subnode, cur_depth);
    }
}

void check_tree(datastore::node_view* parent, size_t cur_depth = 0)
{
    if (cur_depth > datastore::vault::max_tree_depth)
        return;
    cur_depth++;

    auto subnode_names = parent->get_subnode_names();
    CHECK(subnode_names.size() == datastore::node_view::max_num_subnodes);

    for (auto& subnode_name : subnode_names)
    {
        datastore::node_view* subnode = parent->open_subnode(subnode_name);
        CHECK(subnode != nullptr);

        auto values = subnode->get_values();
        CHECK(values.size() == datastore::node_view::max_num_values);

        for (auto& [name, value] : values)
        {
            //TODO: we shouldn't reference the node, use the returned map instead
            CHECK(subnode->get_value_kind(name) == datastore::value_kind::str);
            CHECK(subnode->get_value<std::string>(name) == max_str);
        }

        check_tree(subnode, cur_depth);
    }
}
} // namespace

TEST_CASE("Vault supports basic operations at its elements size limits")
{
    datastore::volume vol1(datastore::volume::priority_class::medium);

    datastore::vault vault;
    CHECK(vault.root()->load_subnode("vol", vol1.root()));

    datastore::node_view* vol_root = vault.root()->open_subnode("vol");
    CHECK(vol_root != nullptr);

    init_tree(vol_root, 1);

    check_tree(vol_root, 1);
}
