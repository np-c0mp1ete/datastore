#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "datastore/volume.hpp"

#include "load_test_common.hpp"

using namespace datastore;

TEST_CASE("Volume supports basic operations at its elements size limits")
{
    BENCHMARK("Benchmark volume tree initialization")
    {
        return load_test::node_create_tree(load_test::vol1.root());
    };

    BENCHMARK("Benchmark saving volume to disk")
    {
        return load_test::vol1.save("vol1.vol");
    };

    BENCHMARK("Benchmark loading volume from disk")
    {
        load_test::vol2 = volume::load("vol1.vol").value();
    };

    BENCHMARK("Benchmark volume tree traversal")
    {
        return load_test::node_get_tree(load_test::vol2.root());
    };

    BENCHMARK("Benchmark volume tree deletion")
    {
        return load_test::node_delete_tree(load_test::vol2.root());
    };
}
