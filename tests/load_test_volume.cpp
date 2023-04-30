#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

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
constexpr size_t max_idx = std::max(node::max_num_subnodes, node::max_num_values);
const auto idx_str = to_string(std::make_index_sequence<max_idx>{});

const std::string max_str = std::string(max_str_value_size_bytes, 'a');

void init_tree(const std::shared_ptr<node>& parent, size_t cur_depth = 0)
{
    if (cur_depth >= volume::max_tree_depth)
        return;
    cur_depth++;

    for (size_t subnode_idx = 0; subnode_idx < node::max_num_subnodes; subnode_idx++)
    {
        std::shared_ptr<node> subnode = parent->create_subnode(idx_str[subnode_idx]);
        CHECK(subnode != nullptr);

        for (size_t value_idx = 0; value_idx < node::max_num_values; value_idx++)
        {
            CHECK(subnode->set_value(idx_str[value_idx], max_str));
        }

        init_tree(subnode, cur_depth);
    }
}

void check_tree(const std::shared_ptr<node>& parent, size_t cur_depth = 0)
{
    if (cur_depth >= volume::max_tree_depth)
        return;
    cur_depth++;

    size_t num_subnodes = 0;
    parent->for_each_subnode([&](const std::shared_ptr<node>& subnode) {
        size_t num_values = 0;
        subnode->for_each_value([&](const attr& a) {
            CHECK(a.get_value_kind() == datastore::value_kind::str);
            CHECK(a.get_value<std::string>() == max_str);
            num_values++;
        });

        CHECK(num_values == datastore::node::max_num_values);

        check_tree(subnode, cur_depth);

        num_subnodes++;
    });

    CHECK(num_subnodes == datastore::node::max_num_subnodes);
}
} // namespace

TEST_CASE("Volume supports basic operations at its elements size limits")
{
    volume vol1(volume::priority_class::medium);

    BENCHMARK("Benchmark volume tree initialization")
    {
        return init_tree(vol1.root());
    };

    BENCHMARK("Benchmark saving volume to disk")
    {
        return vol1.save("vol1.vol");
    };

    std::optional<volume> vol2;

    BENCHMARK("Benchmark loading volume from disk")
    {
        vol2 = volume::load("vol1.vol");
    };

    CHECK(vol2.has_value());

    BENCHMARK("Benchmark volume tree traversal")
    {
        return check_tree(vol2->root());
    };
}
