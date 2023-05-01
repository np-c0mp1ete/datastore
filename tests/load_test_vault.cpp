#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "datastore/vault.hpp"
#include "datastore/volume.hpp"

#include "load_test_common.hpp"

using namespace datastore;

TEST_CASE("Vault supports basic operations at its elements size limits")
{
    vault vault;
    CHECK(vault.root()->load_subnode_tree(load_test::vol1.root()));

    const std::shared_ptr<node_view>& vol_root = vault.root()->open_subnode("vol");
    CHECK(vol_root != nullptr);

    BENCHMARK("Benchmark vault tree initialization")
    {
        return load_test::node_view_create_tree(vol_root, 2);
    };

    BENCHMARK("Benchmark vault tree traversal")
    {
        return load_test::node_view_get_tree(vol_root, 2);
    };

    BENCHMARK("Benchmark vault tree deletion")
    {
        return load_test::node_view_delete_tree(vol_root, 2);
    };

    BENCHMARK("Benchmark vault tree subnode loading")
    {
        return load_test::node_view_load_subnode(vol_root, 2);
    };

    BENCHMARK("Benchmark vault tree subnode unloading")
    {
        return load_test::node_view_unload_subnode(vol_root, 2);
    };
}
