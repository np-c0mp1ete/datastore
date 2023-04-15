#include "datastore/vault.hpp"
#include "datastore/volume.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace datastore::literals;

TEST_CASE("Volume nodes can be loaded into a node view", "[node_view]")
{
    datastore::volume vol(datastore::volume::priority_class::medium);
    vol.root()->set_value("k", "v");

    datastore::vault vault;

    CHECK(vault.root()->get_subnode_names().empty());

    vault.root()->load_subnode("vol", vol.root());

    auto subnode_names = vault.root()->get_subnode_names();
    CHECK(subnode_names.size() == 1);
    CHECK(subnode_names.count("vol") == 1);

    auto vol_view = vault.root()->open_subnode("vol");
    CHECK(vol_view != nullptr);

    auto value_names = vol_view->get_value_names();
    CHECK(value_names.size() == 1);
    CHECK(value_names.count("k") == 1);
    CHECK(vol_view->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vol_view->get_value<std::string>("k") == "v");

    vol.root()->create_subnode("1")->set_value("k1", "v1");
    vault.root()->load_subnode("vol", vol.root()->open_subnode("1"));

    value_names = vol_view->get_value_names();

    CHECK(value_names.size() == 2);

    CHECK(value_names.count("k") == 1);
    CHECK(vol_view->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vol_view->get_value<std::string>("k") == "v");

    CHECK(value_names.count("k1") == 1);
    CHECK(vol_view->get_value_kind("k1") == datastore::value_kind::str);
    CHECK(vol_view->get_value<std::string>("k1") == "v1");
}

TEST_CASE("Volume nodes can be unloaded from a node view", "[node_view]")
{
    datastore::volume vol(datastore::volume::priority_class::medium);
    vol.root()->create_subnode("1.2.3");

    datastore::vault vault;

    CHECK(vault.root()->get_subnode_names().empty());

    auto subnode = vault.root()->load_subnode("vol", vol.root());
    size_t num_unloaded = vault.root()->unload_subnode("vol");

    CHECK(num_unloaded == 4);
    CHECK(vault.root()->get_subnode_names().empty());
    CHECK(vault.root()->open_subnode("vol") == nullptr);
    CHECK(vault.root()->open_subnode("vol.1") == nullptr);
    CHECK(vault.root()->open_subnode("vol.1.2") == nullptr);
    CHECK(vault.root()->open_subnode("vol.1.2.3") == nullptr);

    CHECK(subnode->create_subnode("1") == nullptr);
    CHECK(subnode->name().empty());
}

TEST_CASE("In case of conflicting names, value is taken from a volume with a higher priority", "[node_view]")
{
    datastore::volume vol1(datastore::volume::priority_class::low);
    vol1.root()->set_value("k", "v1");

    datastore::volume vol2(datastore::volume::priority_class::medium);
    vol2.root()->set_value("k", 0_u32);

    datastore::vault vault;

    CHECK(vault.root()->get_subnode_names().empty());

    vault.root()->load_subnode("vol", vol1.root());
    vault.root()->load_subnode("vol", vol2.root());

    auto subnode = vault.root()->open_subnode("vol");
    auto value_names = subnode->get_value_names();
    CHECK(value_names.size() == 1);
    CHECK(value_names.count("k") == 1);
    CHECK(subnode->get_value_kind("k") == datastore::value_kind::u32);
    CHECK(subnode->get_value<uint32_t>("k") == 0_u32);
}

TEST_CASE("Same volume node can be loaded multiple times into different node views", "[node_view]")
{
    datastore::volume vol1(datastore::volume::priority_class::medium);
    vol1.root()->set_value("k", 0_u32);

    datastore::vault vault;

    CHECK(vault.root()->get_subnode_names().empty());

    vault.root()->load_subnode("1", vol1.root());
    vault.root()->load_subnode("2", vol1.root());

    auto subnode_names = vault.root()->get_subnode_names();
    CHECK(subnode_names.size() == 2);
    CHECK(subnode_names.count("1") == 1);
    CHECK(subnode_names.count("2") == 1);

    auto subnode1 = vault.root()->open_subnode("1");
    auto subnode1_value_names = subnode1->get_value_names();
    CHECK(subnode1_value_names.size() == 1);
    CHECK(subnode1_value_names.count("k") == 1);
    CHECK(subnode1->get_value_kind("k") == datastore::value_kind::u32);
    CHECK(subnode1->get_value<uint32_t>("k") == 0_u32);

    auto subnode2 = vault.root()->open_subnode("2");
    auto subnode2_value_names = subnode2->get_value_names();
    CHECK(subnode2_value_names.size() == 1);
    CHECK(subnode2_value_names.count("k") == 1);
    CHECK(subnode2->get_value_kind("k") == datastore::value_kind::u32);
    CHECK(subnode2->get_value<uint32_t>("k") == 0_u32);
}

TEST_CASE("Node can be used in multiple vaults at the same time", "[vault]")
{
    datastore::volume vol(datastore::volume::priority_class::medium);

    datastore::vault vault1;
    vault1.root()->load_subnode("vol", vol.root());

    datastore::vault vault2;
    vault2.root()->load_subnode("vol", vol.root());

    CHECK(vault1.root()->open_subnode("vol") != nullptr);
    CHECK(vault2.root()->open_subnode("vol") != nullptr);
}

TEST_CASE("Node view picks up the changes to the underlying nodes", "[node_view]")
{
    datastore::volume vol(datastore::volume::priority_class::medium);

    datastore::vault vault1;
    vault1.root()->load_subnode("vol", vol.root());

    vol.root()->create_subnode("1");

    CHECK(vault1.root()->open_subnode("vol.1") != nullptr);
}
