#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_floating_point.hpp"

#include <catch2/catch_test_macros.hpp>

#include "datastore/volume.hpp"

TEST_CASE("Volumes can be stored to disk and loaded back", "[volume]")
{
    using namespace datastore::literals;

    datastore::volume vol1(datastore::volume::priority_class::medium);
    vol1.root()->set_value("u32", 1_u32);
    vol1.root()->set_value("u64", 2_u64);
    vol1.root()->set_value("f32", 2.0f);
    vol1.root()->set_value("f64", 1.0);
    vol1.root()->set_value("str", "lorem ipsum");
    vol1.root()->set_value("bin", datastore::binary_blob_t{0xd, 0xe, 0xa, 0xd});

    CHECK(vol1.unload("vol1.vol"));

    auto vol2 = datastore::volume::load("vol1.vol");
    REQUIRE(vol2.has_value());

    auto value_names = vol2->root()->get_value_names();

    CHECK(value_names.count("u32") == 1);
    CHECK(vol2->root()->get_value_kind("u32") == datastore::value_kind::u32);
    CHECK(vol2->root()->get_value<uint32_t>("u32") == 1_u32);

    CHECK(value_names.count("u64") == 1);
    CHECK(vol2->root()->get_value_kind("u64") == datastore::value_kind::u64);
    CHECK(vol2->root()->get_value<uint64_t>("u64") == 2_u64);

    CHECK(value_names.count("f32") == 1);
    CHECK(vol2->root()->get_value_kind("f32") == datastore::value_kind::f32);
    CHECK_THAT(*vol2->root()->get_value<float>("f32"), Catch::Matchers::WithinRel(2.0f));

    CHECK(value_names.count("f64") == 1);
    CHECK(vol2->root()->get_value_kind("f64") == datastore::value_kind::f64);
    CHECK_THAT(*vol2->root()->get_value<double>("f64"), Catch::Matchers::WithinRel(1.0));

    CHECK(value_names.count("str") == 1);
    CHECK(vol2->root()->get_value_kind("str") == datastore::value_kind::str);
    CHECK(vol2->root()->get_value<std::string>("str") == "lorem ipsum");

    CHECK(value_names.count("bin") == 1);
    CHECK(vol2->root()->get_value_kind("bin") == datastore::value_kind::bin);
    CHECK(vol2->root()->get_value<datastore::binary_blob_t>("bin") == datastore::binary_blob_t{0xd, 0xe, 0xa, 0xd});
}
