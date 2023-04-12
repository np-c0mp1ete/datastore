#include <catch2/catch_test_macros.hpp>

#include "datastore/path_view.hpp"

TEST_CASE("Path_view can be constructed from different types", "[path_view]")
{
    datastore::path_view pv = "a.b.c";
    CHECK(pv.size() == 3);

    pv = std::string("a.b.c");
    CHECK(pv.size() == 3);

    pv = std::string_view("a.b.c");
    CHECK(pv.size() == 3);
}

TEST_CASE("Valid path is parsed correctly", "[path_view]")
{
    datastore::path_view pv = "a.b.c";
    CHECK(pv.size() == 3);
    CHECK(pv.composite());
    CHECK(pv.valid());
    CHECK(pv.front() == "a");
    CHECK(pv.back() == "c");
    CHECK(pv.str() == "a.b.c");
}

TEST_CASE("Valid path is modified correctly", "[path_view]")
{
    datastore::path_view pv = "a.b.c";

    pv.pop_front();

    CHECK(pv.size() == 2);
    CHECK(pv.composite());
    CHECK(pv.valid());
    CHECK(pv.front() == "b");
    CHECK(pv.back() == "c");
    CHECK(pv.str() == "b.c");

    pv.pop_back();

    CHECK(pv.size() == 1);
    CHECK(!pv.composite());
    CHECK(pv.valid());
    CHECK(pv.front() == "b");
    CHECK(pv.back() == "b");
    CHECK(pv.str() == "b");

    pv.pop_front();

    CHECK(pv.size() == 0);
    CHECK(!pv.composite());
    CHECK(!pv.valid());
    CHECK(pv.front() == std::nullopt);
    CHECK(pv.back() == std::nullopt);
    CHECK(pv.str().empty());
}

TEST_CASE("Path is validated correctly", "[path_view]")
{
    datastore::path_view pv = "";
    CHECK(!pv.valid());

    pv = ".";
    CHECK(!pv.valid());

    pv = "a.";
    CHECK(!pv.valid());

    pv = ".a";
    CHECK(!pv.valid());

    pv = "a..a";
    CHECK(!pv.valid());

    pv = "/./";
    CHECK(!pv.valid());
}
