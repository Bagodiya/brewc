#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "brewc/token.h"
#include "brewc/value.h"
#include "brewc/value_ops.h"

using namespace brewc;

// these rules used to be private to interpreter.cpp and only got tested through
// whole programs. now that the VM is going to call the same functions they're
// worth pinning down directly, so a change here shows up as a failing unit test
// instead of as one backend quietly disagreeing with the other.

TEST_CASE("int arithmetic stays in ints", "[value_ops]") {
    REQUIRE(apply_int(TokenKind::Plus, 2, 3) == 5);
    REQUIRE(apply_int(TokenKind::Minus, 2, 3) == -1);
    REQUIRE(apply_int(TokenKind::Star, 4, 5) == 20);
    // 7 / 2 truncating to 3 rather than giving 3.5 is the rule both backends
    // have to agree on, so it gets its own check.
    REQUIRE(apply_int(TokenKind::Slash, 7, 2) == 3);
    REQUIRE(apply_int(TokenKind::Percent, 7, 2) == 1);
}

TEST_CASE("dividing an int by zero is an error, not undefined behaviour", "[value_ops]") {
    REQUIRE_THROWS_AS(apply_int(TokenKind::Slash, 1, 0), std::runtime_error);
    REQUIRE_THROWS_AS(apply_int(TokenKind::Percent, 1, 0), std::runtime_error);
}

TEST_CASE("a non-arithmetic operator is rejected by apply_int", "[value_ops]") {
    REQUIRE_THROWS_AS(apply_int(TokenKind::Less, 1, 2), std::runtime_error);
}

TEST_CASE("float arithmetic keeps the fraction", "[value_ops]") {
    REQUIRE(apply_float(TokenKind::Plus, 1.0, 2.5) == 3.5);
    REQUIRE(apply_float(TokenKind::Slash, 7.0, 2.0) == 3.5);
}

TEST_CASE("modulo on floats is rejected", "[value_ops]") {
    REQUIRE_THROWS_AS(apply_float(TokenKind::Percent, 7.0, 2.0), std::runtime_error);
}

TEST_CASE("dividing a float by zero follows IEEE instead of throwing", "[value_ops]") {
    double result = apply_float(TokenKind::Slash, 1.0, 0.0);
    REQUIRE(std::isinf(result));
    REQUIRE(result > 0.0);
}

TEST_CASE("is_number covers ints and floats only", "[value_ops]") {
    REQUIRE(is_number(Value{int64_t{1}}));
    REQUIRE(is_number(Value{1.5}));
    REQUIRE_FALSE(is_number(Value{true}));
    REQUIRE_FALSE(is_number(Value{std::string("2")}));
    REQUIRE_FALSE(is_number(Value{}));
}

TEST_CASE("to_double widens an int and leaves a float alone", "[value_ops]") {
    REQUIRE(to_double(Value{int64_t{3}}) == 3.0);
    REQUIRE(to_double(Value{2.5}) == 2.5);
}

TEST_CASE("is_comparison picks out the six comparison operators", "[value_ops]") {
    REQUIRE(is_comparison(TokenKind::EqualEqual));
    REQUIRE(is_comparison(TokenKind::BangEqual));
    REQUIRE(is_comparison(TokenKind::Less));
    REQUIRE(is_comparison(TokenKind::Greater));
    REQUIRE(is_comparison(TokenKind::LessEqual));
    REQUIRE(is_comparison(TokenKind::GreaterEqual));
    REQUIRE_FALSE(is_comparison(TokenKind::Plus));
    REQUIRE_FALSE(is_comparison(TokenKind::AmpAmp));
}

TEST_CASE("only nil and false are falsy", "[value_ops]") {
    REQUIRE_FALSE(is_truthy(Value{}));
    REQUIRE_FALSE(is_truthy(Value{false}));
    REQUIRE(is_truthy(Value{true}));
    // zero and the empty string are still true, which is the part people expect
    // to go the other way.
    REQUIRE(is_truthy(Value{int64_t{0}}));
    REQUIRE(is_truthy(Value{0.0}));
    REQUIRE(is_truthy(Value{std::string("")}));
}

TEST_CASE("equality needs both sides to be the same kind", "[value_ops]") {
    REQUIRE(values_equal(Value{true}, Value{true}));
    REQUIRE_FALSE(values_equal(Value{true}, Value{false}));
    REQUIRE(values_equal(Value{std::string("hi")}, Value{std::string("hi")}));
    REQUIRE(values_equal(Value{}, Value{}));
    REQUIRE_FALSE(values_equal(Value{true}, Value{std::string("true")}));
}

TEST_CASE("comparing two ints does not go through doubles", "[value_ops]") {
    // both of these are past 2^53, so if they were widened to double they would
    // land on the same value and compare equal.
    Value a = int64_t{9007199254740993};
    Value b = int64_t{9007199254740992};
    REQUIRE(compare(TokenKind::Greater, a, b));
    REQUIRE_FALSE(compare(TokenKind::EqualEqual, a, b));
}

TEST_CASE("an int and a float can be compared", "[value_ops]") {
    REQUIRE(compare(TokenKind::Less, Value{int64_t{1}}, Value{1.5}));
    REQUIRE(compare(TokenKind::EqualEqual, Value{int64_t{2}}, Value{2.0}));
    REQUIRE(compare(TokenKind::GreaterEqual, Value{3.0}, Value{int64_t{3}}));
}

TEST_CASE("non-numbers get equality but not ordering", "[value_ops]") {
    Value a = std::string("apple");
    Value b = std::string("banana");
    REQUIRE(compare(TokenKind::BangEqual, a, b));
    REQUIRE_THROWS_AS(compare(TokenKind::Less, a, b), std::runtime_error);
    REQUIRE_THROWS_AS(compare(TokenKind::Less, Value{true}, Value{false}), std::runtime_error);
}

TEST_CASE("compare_numbers rejects an operator that is not a comparison", "[value_ops]") {
    REQUIRE_THROWS_AS(compare_numbers(TokenKind::Plus, 1, 2), std::runtime_error);
}
