#include "brewc/value_ops.h"

#include <string>

namespace brewc {

int64_t apply_int(TokenKind op, int64_t a, int64_t b) {
    switch (op) {
    case TokenKind::Plus:
        return a + b;
    case TokenKind::Minus:
        return a - b;
    case TokenKind::Star:
        return a * b;
    case TokenKind::Slash:
        if (b == 0) {
            throw std::runtime_error("division by zero");
        }
        return a / b;
    case TokenKind::Percent:
        if (b == 0) {
            throw std::runtime_error("modulo by zero");
        }
        return a % b;
    default:
        throw std::runtime_error("operator is not arithmetic");
    }
}

double apply_float(TokenKind op, double a, double b) {
    switch (op) {
    case TokenKind::Plus:
        return a + b;
    case TokenKind::Minus:
        return a - b;
    case TokenKind::Star:
        return a * b;
    case TokenKind::Slash:
        return a / b;
    default:
        // there's no sensible float modulo here, and that's the only operator
        // that falls through to this point.
        throw std::runtime_error("operator is not valid on floats");
    }
}

bool is_number(const Value& value) {
    return is_int(value) || is_float(value);
}

double to_double(const Value& value) {
    if (is_int(value)) {
        return static_cast<double>(std::get<int64_t>(value));
    }
    return std::get<double>(value);
}

bool is_comparison(TokenKind op) {
    switch (op) {
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
    case TokenKind::Less:
    case TokenKind::Greater:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
        return true;
    default:
        return false;
    }
}

bool is_truthy(const Value& value) {
    if (is_nil(value)) {
        return false;
    }
    if (is_bool(value)) {
        return std::get<bool>(value);
    }
    return true;
}

bool values_equal(const Value& a, const Value& b) {
    if (is_bool(a) && is_bool(b)) {
        return std::get<bool>(a) == std::get<bool>(b);
    }
    if (is_string(a) && is_string(b)) {
        return std::get<std::string>(a) == std::get<std::string>(b);
    }
    if (is_nil(a) && is_nil(b)) {
        return true;
    }
    return false;
}

bool compare(TokenKind op, const Value& lhs, const Value& rhs) {
    // two ints compare as ints so nothing gets rounded on the way. any other pair
    // of numbers goes through doubles, which covers float against float as well as
    // the mixed case, so 1 < 1.5 answers instead of erroring out.
    if (is_int(lhs) && is_int(rhs)) {
        return compare_numbers(op, std::get<int64_t>(lhs), std::get<int64_t>(rhs));
    }
    if (is_number(lhs) && is_number(rhs)) {
        return compare_numbers(op, to_double(lhs), to_double(rhs));
    }

    if (op == TokenKind::EqualEqual) {
        return values_equal(lhs, rhs);
    }
    if (op == TokenKind::BangEqual) {
        return !values_equal(lhs, rhs);
    }

    throw std::runtime_error("cannot order " + type_name(lhs) + " and " + type_name(rhs));
}

} // namespace brewc
