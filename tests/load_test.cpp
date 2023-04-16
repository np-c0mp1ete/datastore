#include <catch2/catch_test_macros.hpp>

#include "datastore/volume.hpp"

TEST_CASE("Volume supports basic operations at its elements size limits")
{
    datastore::volume vol1(datastore::volume::priority_class::medium);

    constexpr size_t max_depth = datastore::volume::max_tree_depth;
    constexpr size_t num_values = datastore::node::max_num_values;

    static std::string longest_str = std::string(datastore::max_str_value_size_bytes, 'a');

    datastore::node* parent = vol1.root();
    for (size_t depth = 0; depth < max_depth; depth++)
    {
        datastore::node* subnode = parent->create_subnode(std::to_string(depth));
        for (size_t i = 0; i < num_values; i++)
        {
            subnode->set_value(std::to_string(i), longest_str);
        }
        parent = subnode;
    }

    CHECK(vol1.unload("vol1.vol"));

    auto vol2 = datastore::volume::load("vol1.vol");
    CHECK(vol2.has_value());

    parent = vol2->root();
    for (size_t depth = 0; depth < max_depth; depth++)
    {
        datastore::node* subnode = parent->open_subnode(std::to_string(depth));
        for (size_t i = 0; i < num_values; i++)
        {
            CHECK(subnode->get_value<std::string>(std::to_string(i)) == longest_str);
        }
        parent = subnode;
    }
}
