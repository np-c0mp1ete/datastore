#include "datastore/vault.hpp"
#include "datastore/volume.hpp"

#include <iostream>
#include <sstream>
#include <catch2/catch_test_macros.hpp>

using namespace datastore;
using namespace datastore::literals;

TEST_CASE("Volume nodes can be loaded into a node view", "[node_view]")
{
    volume vol("vol", volume::priority_class::medium);
    vol.root()->set_value("k", "v");

    vault vault;

    vault.root()->load_subnode_tree(vol.root());

    const auto& vol_view = vault.root()->open_subnode("vol");
    CHECK(vol_view != nullptr);

    CHECK(vol_view->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vol_view->get_value<std::string>("k") == "v");

    vol.root()->create_subnode("1")->set_value("k1", "v1");
    vault.root()->load_subnode_tree(vol.root()->open_subnode("1"));

    CHECK(vol_view->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vol_view->get_value<std::string>("k") == "v");

    CHECK(vault.root()->open_subnode("1")->get_value_kind("k1") == datastore::value_kind::str);
    CHECK(vault.root()->open_subnode("1")->get_value<std::string>("k1") == "v1");
}

TEST_CASE("Volume nodes can be unloaded from a node view", "[node_view]")
{
    volume vol("vol", volume::priority_class::medium);
    vol.root()->create_subnode("1.2.3");

    vault vault;

    const std::shared_ptr<node_view>& subnode = vault.root()->load_subnode_tree(vol.root());

    CHECK(vault.root()->unload_subnode_tree("vol"));
    CHECK(vault.root()->open_subnode("vol") == nullptr);
    CHECK(vault.root()->open_subnode("vol.1") == nullptr);
    CHECK(vault.root()->open_subnode("vol.1.2") == nullptr);
    CHECK(vault.root()->open_subnode("vol.1.2.3") == nullptr);

    CHECK(subnode->create_subnode("1") == nullptr);
    CHECK(subnode->expired());
}

TEST_CASE("Volume nodes can be created using the node_view API", "[node_view]")
{
    volume vol("vol", volume::priority_class::medium);
    vault vault;
    vault.root()->load_subnode_tree(vol.root());

    CHECK(vault.root()->create_subnode("vol.1") != nullptr);
    CHECK(vol.root()->open_subnode("1") != nullptr);
}

TEST_CASE("In case of conflicting names, value is taken from a volume with a higher priority", "[node_view]")
{
    volume vol1("vol", volume::priority_class::low);
    vol1.root()->set_value("k", "v1");

    volume vol2("vol", volume::priority_class::medium);
    vol2.root()->set_value("k", 0_u32);

    vault vault;

    vault.root()->load_subnode_tree(vol1.root());
    vault.root()->load_subnode_tree(vol2.root());

    const auto& subnode = vault.root()->open_subnode("vol");
    CHECK(subnode->get_value_kind("k") == datastore::value_kind::u32);
    CHECK(subnode->get_value<uint32_t>("k") == 0_u32);
}

TEST_CASE("Same volume node can be loaded multiple times into different node views", "[node_view]")
{
    volume vol1("vol", volume::priority_class::medium);
    vol1.root()->set_value("k", 0_u32);

    vault vault;

    vault.root()->load_subnode_tree(vol1.root());
    vault.root()->open_subnode("vol")->load_subnode_tree(vol1.root());

    const auto& subnode1 = vault.root()->open_subnode("vol");
    CHECK(subnode1 != nullptr);

    CHECK(subnode1->get_value_kind("k") == datastore::value_kind::u32);
    CHECK(subnode1->get_value<uint32_t>("k") == 0_u32);

    const auto& subnode2 = vault.root()->open_subnode("vol.vol");
    CHECK(subnode2 != nullptr);

    CHECK(subnode2->get_value_kind("k") == datastore::value_kind::u32);
    CHECK(subnode2->get_value<uint32_t>("k") == 0_u32);
}

TEST_CASE("Node can be used in multiple vaults at the same time", "[vault]")
{
    volume vol("vol", volume::priority_class::medium);

    vault vault1;
    vault1.root()->load_subnode_tree(vol.root());

    vault vault2;
    vault2.root()->load_subnode_tree(vol.root());

    CHECK(vault1.root()->open_subnode("vol") != nullptr);
    CHECK(vault2.root()->open_subnode("vol") != nullptr);
}

TEST_CASE("Node view picks up the changes to the underlying nodes", "[node_view]")
{
    volume vol("vol", volume::priority_class::medium);

    vault vault;
    vault.root()->load_subnode_tree(vol.root());

    vol.root()->create_subnode("1");
    CHECK(vault.root()->open_subnode("vol.1") != nullptr);

    size_t num_deleted = vol.root()->delete_subnode_tree("1");
    CHECK(num_deleted == 1);
    CHECK(vault.root()->open_subnode("vol.1") == nullptr);
}

TEST_CASE("Volume priority is respected by a node view after setting a value outside of a vault", "[node_view]")
{
    volume vol1("vol", volume::priority_class::medium);

    volume vol2("vol", volume::priority_class::low);
    vol2.root()->create_subnode("1")->set_value("k", "v2");

    vault vault;
    vault.root()->load_subnode_tree(vol1.root());
    vault.root()->load_subnode_tree(vol2.root());

    CHECK(vault.root()->open_subnode("vol.1")->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vault.root()->open_subnode("vol.1")->get_value<std::string>("k") == "v2");

    vol1.root()->create_subnode("1")->set_value("k", 1_u32);

    CHECK(vault.root()->open_subnode("vol.1")->get_value_kind("k") == datastore::value_kind::u32);
    CHECK(vault.root()->open_subnode("vol.1")->get_value<uint32_t>("k") == 1_u32);
}

TEST_CASE("When a node has multiple node views, "
          "all of them are updated in case a node gets deleted outside of a vault", "[node_view]")
{
    volume vol("vol", volume::priority_class::medium);
    vol.root()->create_subnode("1")->set_value("k", "v");

    vault vault;
    vault.root()->load_subnode_tree(vol.root());
    vault.root()->open_subnode("vol")->create_subnode("1")->load_subnode_tree(vol.root());
    vault.root()->open_subnode("vol")->create_subnode("2")->load_subnode_tree(vol.root());

    CHECK(vault.root()->open_subnode("vol.1") != nullptr);
    CHECK(vault.root()->open_subnode("vol.1")->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vault.root()->open_subnode("vol.1")->get_value<std::string>("k") == "v");

    CHECK(vault.root()->open_subnode("vol.1.vol.1") != nullptr);
    CHECK(vault.root()->open_subnode("vol.1.vol.1")->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vault.root()->open_subnode("vol.1.vol.1")->get_value<std::string>("k") == "v");

    CHECK(vault.root()->open_subnode("vol.2.vol.1") != nullptr);
    CHECK(vault.root()->open_subnode("vol.2.vol.1")->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vault.root()->open_subnode("vol.2.vol.1")->get_value<std::string>("k") == "v");

    size_t num_deleted = vol.root()->delete_subnode_tree("1");

    CHECK(num_deleted == 1);
    CHECK(vault.root()->open_subnode("vol.1") != nullptr);
    CHECK(vault.root()->open_subnode("vol.1.vol.1") == nullptr);
    CHECK(vault.root()->open_subnode("vol.2.vol.1") == nullptr);
}

TEST_CASE("If a node gets deleted and recreated outside of a vault, respective node view gets updated", "[node_view]")
{
    volume vol("vol", volume::priority_class::medium);
    vol.root()->create_subnode("1")->set_value("k", "v");

    vault vault;
    vault.root()->load_subnode_tree(vol.root());

    size_t num_deleted = vol.root()->delete_subnode_tree("1");
    CHECK(num_deleted == 1);

    vol.root()->create_subnode("1")->set_value("k", 1_u64);

    CHECK(vault.root()->open_subnode("vol.1") != nullptr);
    CHECK(vault.root()->open_subnode("vol.1")->get_value_kind("k") == datastore::value_kind::u64);
    CHECK(vault.root()->open_subnode("vol.1")->get_value<uint64_t>("k") == 1_u64);
}

TEST_CASE("Node views get updated accordingly when a node tree is deleted")
{
    volume vol1("vol", volume::priority_class::medium);
    vol1.root()->create_subnode("1")->set_value("k", "v1");
    vol1.root()->create_subnode("1.2.3");

    volume vol2("vol", volume::priority_class::low);
    vol2.root()->create_subnode("1")->set_value("k", "v2");

    vault vault;
    vault.root()->load_subnode_tree(vol1.root());
    vault.root()->load_subnode_tree(vol2.root());

    CHECK(vault.root()->open_subnode("vol.1") != nullptr);
    CHECK(vault.root()->open_subnode("vol.1")->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vault.root()->open_subnode("vol.1")->get_value<std::string>("k") == "v1");

    CHECK(vol1.root()->delete_subnode_tree("1"));
    CHECK(vault.root()->open_subnode("vol.1.2") == nullptr);
    CHECK(vault.root()->open_subnode("vol.1.2.3") == nullptr);

    CHECK(vault.root()->open_subnode("vol.1") != nullptr);
    CHECK(vault.root()->open_subnode("vol.1")->get_value_kind("k") == datastore::value_kind::str);
    CHECK(vault.root()->open_subnode("vol.1")->get_value<std::string>("k") == "v2");
}

TEST_CASE("Volume is accessible after vault destruction")
{
    volume vol("vol", volume::priority_class::medium);
    vol.root()->create_subnode("1.2");

    {
        vault vault;
        vault.root()->load_subnode_tree(vol.root());
    }

    CHECK(vol.root()->delete_subnode_tree("1"));
}

TEST_CASE("Loaded volume nodes are accessible in a vault after volume destruction")
{
    vault vault;

    {
        volume vol("vol", volume::priority_class::medium);
        vol.root()->create_subnode("1.2");

        vault.root()->load_subnode_tree(vol.root());
    }

    CHECK(vault.root()->open_subnode("vol.1.2") != nullptr);
}

TEST_CASE("Unloading non-existent node fails")
{
    volume vol("vol", volume::priority_class::medium);

    vault vault;
    vault.root()->load_subnode_tree(vol.root());

    const size_t num_unloaded = vault.root()->unload_subnode_tree("root");

    CHECK(num_unloaded == 0);
}
