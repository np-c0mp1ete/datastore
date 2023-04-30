#include <catch2/catch_test_macros.hpp>

#include "datastore/vault.hpp"
#include "datastore/volume.hpp"

#include <array>

using namespace datastore;

namespace
{
template <size_t... Ints>
auto to_string(std::integer_sequence<size_t, Ints...>)
{
    return std::array<std::string, sizeof...(Ints)>{{std::to_string(Ints)...}};
}

// Cache stringified indices
constexpr size_t max_idx =
    std::max(node_view::max_num_subviews, node_view::max_num_values);
const auto idx_str = to_string(std::make_index_sequence<max_idx>{});

const std::string max_str = std::string(max_str_value_size_bytes, 'a');

void init_tree(const std::shared_ptr<node_view>& parent, size_t cur_depth = 0)
{
    if (cur_depth >= vault::max_tree_depth)
        return;
    cur_depth++;

    for (size_t subnode_idx = 0; subnode_idx < node_view::max_num_subviews; subnode_idx++)
    {
        std::shared_ptr<node_view> subnode = parent->create_subnode(idx_str[subnode_idx]);
        CHECK(subnode != nullptr);

        for (size_t value_idx = 0; value_idx < node_view::max_num_values; value_idx++)
        {
            CHECK(subnode->set_value(idx_str[value_idx], max_str));
        }

        init_tree(subnode, cur_depth);
    }
}

void check_tree(const std::shared_ptr<node_view>& parent, size_t cur_depth = 0)
{
    if (cur_depth >= vault::max_tree_depth)
        return;
    cur_depth++;

    size_t num_subnodes = 0;
    parent->for_each_subnode([&](const std::shared_ptr<node_view>& subnode) {
        size_t num_values = 0;
        subnode->for_each_value([&](const attr& a) {
            CHECK(a.get_value_kind() == datastore::value_kind::str);
            CHECK(a.get_value<std::string>() == max_str);
            num_values++;
        });

        CHECK(num_values == datastore::node_view::max_num_values);

        check_tree(subnode, cur_depth);

        num_subnodes++;
    });

    CHECK(num_subnodes == datastore::node_view::max_num_subviews);
}
} // namespace

TEST_CASE("Vault supports basic operations at its elements size limits")
{
    volume vol1(volume::priority_class::medium);

    vault vault;
    CHECK(vault.root()->load_subnode_tree("vol", vol1.root()));

    std::shared_ptr<node_view> vol_root = vault.root()->open_subnode("vol");
    CHECK(vol_root != nullptr);

    init_tree(vol_root, 1);

    check_tree(vol_root, 1);
}
