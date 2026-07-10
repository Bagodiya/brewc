#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>

#include "brewc/environment.h"
#include "brewc/value.h"

using namespace brewc;

TEST_CASE("a defined name can be read back", "[env]") {
    Environment env;
    env.define("x", int64_t{10});

    Value* found = env.get("x");
    REQUIRE(found != nullptr);
    REQUIRE(is_int(*found));
    REQUIRE(std::get<int64_t>(*found) == 10);
}

TEST_CASE("looking up an unknown name gives null", "[env]") {
    Environment env;
    REQUIRE(env.get("missing") == nullptr);
}

TEST_CASE("defining a name twice overwrites it", "[env]") {
    Environment env;
    env.define("x", int64_t{1});
    env.define("x", int64_t{2});

    REQUIRE(std::get<int64_t>(*env.get("x")) == 2);
}

TEST_CASE("a child scope sees names from its parent", "[env]") {
    auto outer = std::make_shared<Environment>();
    outer->define("g", std::string("hi"));

    Environment inner(outer);
    Value* found = inner.get("g");
    REQUIRE(found != nullptr);
    REQUIRE(std::get<std::string>(*found) == "hi");
}

TEST_CASE("a child binding shadows the parent without changing it", "[env]") {
    auto outer = std::make_shared<Environment>();
    outer->define("x", int64_t{1});

    Environment inner(outer);
    inner.define("x", int64_t{99});

    REQUIRE(std::get<int64_t>(*inner.get("x")) == 99);
    REQUIRE(std::get<int64_t>(*outer->get("x")) == 1);
}

TEST_CASE("assign updates an existing binding in place", "[env]") {
    Environment env;
    env.define("x", int64_t{1});

    REQUIRE(env.assign("x", int64_t{5}));
    REQUIRE(std::get<int64_t>(*env.get("x")) == 5);
}

TEST_CASE("assign walks outward to find the binding", "[env]") {
    auto outer = std::make_shared<Environment>();
    outer->define("x", int64_t{1});

    Environment inner(outer);
    REQUIRE(inner.assign("x", int64_t{7}));
    // it changed the outer slot, not a fresh one in the inner scope.
    REQUIRE(std::get<int64_t>(*outer->get("x")) == 7);
}

TEST_CASE("assign to an undefined name fails", "[env]") {
    Environment env;
    REQUIRE_FALSE(env.assign("nope", int64_t{0}));
    REQUIRE(env.get("nope") == nullptr);
}
