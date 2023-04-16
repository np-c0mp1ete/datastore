#include <catch2/catch_test_macros.hpp>

#include "datastore/node.hpp"
#include "datastore/volume.hpp"

TEST_CASE("Nodes can be created and opened", "[node]")
{
    datastore::volume vol(datastore::volume::priority_class::medium);
    auto node_1 = vol.root()->create_subnode("1");

    CHECK(node_1 != nullptr);
    CHECK(vol.root()->create_subnode("1") == node_1);
    CHECK(vol.root()->open_subnode("1") == node_1);

    auto node_1_2_3 = node_1->create_subnode("2.3");

    CHECK(node_1_2_3 != nullptr);
    CHECK(vol.root()->create_subnode("1.2.3") == node_1_2_3);
    CHECK(vol.root()->open_subnode("1.2.3") == node_1_2_3);
}

TEST_CASE("Node trees can be deleted", "[node]")
{
    datastore::volume vol(datastore::volume::priority_class::medium);
    vol.root()->create_subnode("1.2");
    vol.root()->create_subnode("1.3");
    size_t num_deleted = vol.root()->delete_subnode_tree("1");

    CHECK(num_deleted == 3);
    CHECK(vol.root()->open_subnode("1") == nullptr);
    CHECK(vol.root()->open_subnode("1.2") == nullptr);
    CHECK(vol.root()->open_subnode("1.3") == nullptr);

    vol.root()->create_subnode("1.2.3");
    num_deleted = vol.root()->delete_subnode_tree("1.2.3");

    CHECK(num_deleted == 1);
    CHECK(vol.root()->open_subnode("1.2.3") == nullptr);
}

TEST_CASE("Data type of the value with the given name can be retrieved", "[node]")
{
    datastore::volume vol(datastore::volume::priority_class::medium);
    vol.root()->set_value("k", "v");

    CHECK(vol.root()->get_value_kind("k") == datastore::value_kind::str);
}

TEST_CASE("Value with the given name can be retvieved", "[node]")
{
    datastore::volume vol(datastore::volume::priority_class::medium);
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
    datastore::volume vol(datastore::volume::priority_class::medium);
    vol.root()->set_value("k", "v");

    const size_t num_deleted = vol.root()->delete_value("k");

    CHECK(num_deleted == 1);
    CHECK(!vol.root()->get_value<std::string>("k").has_value());
}
