#include <catch2/catch_test_macros.hpp>

#include "datastore/volume.hpp"

static const std::string max_str = std::string(datastore::max_str_value_size_bytes, 'a');

void init_tree(datastore::node* parent, size_t cur_depth = 0)
{
    if (cur_depth > datastore::volume::max_tree_depth)
        return;
    cur_depth++;

    for (size_t subnode_idx = 0; subnode_idx < datastore::node::max_num_subnodes; subnode_idx++)
    {
        datastore::node* subnode = parent->create_subnode(std::to_string(subnode_idx));
        CHECK(subnode != nullptr);

        for (size_t value_idx = 0; value_idx < datastore::node::max_num_values; value_idx++)
        {
            CHECK(subnode->set_value(std::to_string(value_idx), max_str));
        }

        init_tree(subnode, cur_depth);
    }
}

void check_tree(datastore::node* parent, size_t cur_depth = 0)
{
    if (cur_depth > datastore::volume::max_tree_depth)
        return;
    cur_depth++;

    auto subnode_names = parent->get_subnode_names();
    CHECK(subnode_names.size() == datastore::node::max_num_subnodes);

    for (auto& subnode_name : subnode_names)
    {
        datastore::node* subnode = parent->open_subnode(subnode_name);
        CHECK(subnode != nullptr);

        auto values = subnode->get_values();
        CHECK(values.size() == datastore::node::max_num_values);

        for (auto& [name, value] : values)
        {
            CHECK(subnode->get_value_kind(name) == datastore::value_kind::str);
            CHECK(subnode->get_value<std::string>(name) == max_str);
        }

        check_tree(subnode, cur_depth);
    }
}

TEST_CASE("Volume supports basic operations at its elements size limits")
{
    datastore::volume vol1(datastore::volume::priority_class::medium);

    init_tree(vol1.root());

    CHECK(vol1.unload("vol1.vol"));

    auto vol2 = datastore::volume::load("vol1.vol");
    CHECK(vol2.has_value());

    check_tree(vol2->root());
}
