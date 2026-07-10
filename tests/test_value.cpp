#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <memory>
#include <string>

#include "brewc/ast.h"
#include "brewc/token.h"
#include "brewc/value.h"

using namespace brewc;

// note: the alternatives have to be built with explicit types. a bare 42 is an
// int and could just as well go to int64_t, double or bool, which the variant
// rejects as ambiguous, so the tests spell out int64_t{...} and std::string(...).

TEST_CASE("an int value knows it is an int", "[value]") {
    Value v = int64_t{42};
    REQUIRE(is_int(v));
    REQUIRE_FALSE(is_float(v));
    REQUIRE_FALSE(is_bool(v));
    REQUIRE_FALSE(is_string(v));
    REQUIRE_FALSE(is_nil(v));
}

TEST_CASE("a float value knows it is a float", "[value]") {
    Value v = 3.14;
    REQUIRE(is_float(v));
    REQUIRE_FALSE(is_int(v));
}

TEST_CASE("a bool value knows it is a bool", "[value]") {
    Value v = true;
    REQUIRE(is_bool(v));
    REQUIRE_FALSE(is_int(v));
}

TEST_CASE("a string value knows it is a string", "[value]") {
    Value v = std::string("hello");
    REQUIRE(is_string(v));
    REQUIRE_FALSE(is_int(v));
}

TEST_CASE("a default value is nil", "[value]") {
    Value v;
    REQUIRE(is_nil(v));
}

TEST_CASE("a function value knows it is a function", "[value]") {
    // an empty-body, no-param decl is enough to hang a Function value off of.
    FnDecl decl(Token(TokenKind::Identifier, "add", 1, 1), {}, nullptr);
    // no closure scope here — these value-level tests only care about the decl
    // half, so the captured scope stays null.
    Value v = Function{&decl, nullptr};
    REQUIRE(is_function(v));
    REQUIRE_FALSE(is_int(v));
    REQUIRE_FALSE(is_string(v));
    REQUIRE_FALSE(is_nil(v));
}

TEST_CASE("type_name reports each variant", "[value]") {
    FnDecl decl(Token(TokenKind::Identifier, "f", 1, 1), {}, nullptr);
    REQUIRE(type_name(Value{}) == "nil");
    REQUIRE(type_name(Value{int64_t{1}}) == "int");
    REQUIRE(type_name(Value{2.5}) == "float");
    REQUIRE(type_name(Value{false}) == "bool");
    REQUIRE(type_name(Value{std::string("x")}) == "string");
    REQUIRE(type_name(Value{Function{&decl, nullptr}}) == "function");
}

TEST_CASE("to_string renders ints plainly", "[value]") {
    REQUIRE(to_string(Value{int64_t{0}}) == "0");
    REQUIRE(to_string(Value{int64_t{42}}) == "42");
    REQUIRE(to_string(Value{int64_t{-7}}) == "-7");
}

TEST_CASE("to_string renders bools as true/false", "[value]") {
    REQUIRE(to_string(Value{true}) == "true");
    REQUIRE(to_string(Value{false}) == "false");
}

TEST_CASE("to_string renders nil", "[value]") {
    REQUIRE(to_string(Value{}) == "nil");
}

TEST_CASE("to_string leaves strings unquoted", "[value]") {
    REQUIRE(to_string(Value{std::string("hi there")}) == "hi there");
}

TEST_CASE("to_string keeps a decimal point on whole floats", "[value]") {
    // the point of the .0 patch-up: a float that happens to be a round number
    // should still look like a float and not an int.
    REQUIRE(to_string(Value{3.0}) == "3.0");
    REQUIRE(to_string(Value{-1.0}) == "-1.0");
}

TEST_CASE("to_string keeps the fractional part of a float", "[value]") {
    REQUIRE(to_string(Value{1.5}) == "1.5");
}

TEST_CASE("to_string prints a function with its name", "[value]") {
    FnDecl decl(Token(TokenKind::Identifier, "greet", 1, 1), {}, nullptr);
    REQUIRE(to_string(Value{Function{&decl, nullptr}}) == "<fn greet>");
}
