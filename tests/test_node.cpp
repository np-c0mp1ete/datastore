#include <catch2/catch_test_macros.hpp>

#include "datastore/node.hpp"
#include "datastore/volume.hpp"

#include <iostream>

using namespace datastore;

TEST_CASE("Nodes can be created and opened", "[node]")
{
    volume vol(volume::priority_class::medium);
    const auto& node_1 = vol.root()->create_subnode("1");

    CHECK(node_1 != nullptr);
    CHECK(vol.root()->create_subnode("1") == node_1);
    CHECK(vol.root()->open_subnode("1") == node_1);

    const auto& node_1_2_3 = node_1->create_subnode("2.3");

    CHECK(node_1_2_3 != nullptr);
    CHECK(vol.root()->create_subnode("1.2.3") == node_1_2_3);
    CHECK(vol.root()->open_subnode("1.2.3") == node_1_2_3);
}

TEST_CASE("Node trees can be deleted", "[node]")
{
    volume vol(volume::priority_class::medium);

    SECTION("Non-leaf nodes are deleted as expected")
    {
        vol.root()->create_subnode("1.2");
        vol.root()->create_subnode("1.3");
        bool success = vol.root()->delete_subnode_tree("1");

        CHECK(success);
        CHECK(vol.root()->open_subnode("1") == nullptr);
        CHECK(vol.root()->open_subnode("1.2") == nullptr);
        CHECK(vol.root()->open_subnode("1.3") == nullptr);
    }

    SECTION("Leaf nodes are deleted as expected")
    {
        vol.root()->create_subnode("1");
        bool success = vol.root()->delete_subnode_tree("1");

        CHECK(success);
        CHECK(vol.root()->open_subnode("1") == nullptr);
    }
}

TEST_CASE("Data type of the value with the given name can be retrieved", "[node]")
{
    volume vol(volume::priority_class::medium);
    vol.root()->set_value("k", "v");

    CHECK(vol.root()->get_value_kind("k") == datastore::value_kind::str);
}

TEST_CASE("Value with the given name can be retvieved", "[node]")
{
    volume vol(volume::priority_class::medium);
    vol.root()->set_value("k", "v");

    CHECK(*vol.root()->get_value<std::string>("k") == "v");

    CHECK_FALSE(vol.root()->get_value<uint32_t>("k"));
    CHECK_FALSE(vol.root()->get_value<uint64_t>("k"));
    CHECK_FALSE(vol.root()->get_value<float>("k"));
    CHECK_FALSE(vol.root()->get_value<double>("k"));
    CHECK_FALSE(vol.root()->get_value<datastore::binary_blob_t>("k"));
}

TEST_CASE("Value with the given name can be deleted", "[node]")
{
    volume vol(volume::priority_class::medium);
    vol.root()->set_value("k", "v");

    const size_t num_deleted = vol.root()->delete_value("k");

    CHECK(num_deleted == 1);
    CHECK(!vol.root()->get_value<std::string>("k").has_value());
}

TEST_CASE("Subnodes can be iterated over", "[node]")
{
    volume vol(volume::priority_class::medium);
    vol.root()->create_subnode("1");
    vol.root()->create_subnode("2");

    size_t num_subnodes = 0;
    bool subnode1_present = false;
    bool subnode2_present = false;
    vol.root()->for_each_subnode([&](const std::shared_ptr<node>& subnode) {
        subnode1_present = subnode1_present || subnode->name() == "1";
        subnode2_present = subnode2_present || subnode->name() == "2";
        num_subnodes++;
    });

    CHECK(num_subnodes == 2);
    CHECK(subnode1_present);
    CHECK(subnode2_present);
}
