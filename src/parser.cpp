#include "brewc/parser.h"

#include <stdexcept>

namespace brewc {

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), current_(0) {}

bool Parser::is_at_end() const {
    return peek().kind == TokenKind::End;
}

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

const Token& Parser::advance() {
    if (!is_at_end()) {
        current_++;
    }
    return previous();
}

bool Parser::check(TokenKind kind) const {
    if (is_at_end()) {
        return false;
    }
    return peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::consume(TokenKind kind, const std::string& message) {
    if (check(kind)) {
        return advance();
    }
    // placeholder until step 31 wires up real diagnostics with line/column.
    throw std::runtime_error(message);
}

namespace {

// how tightly each binary operator binds. higher number wins, so it grabs its
// operands before a weaker neighbour does. anything that isn't a binary operator
// returns 0, which the climbing loop reads as "stop, this isn't ours".
int binary_precedence(TokenKind kind) {
    switch (kind) {
    case TokenKind::PipePipe:
        return 1;
    case TokenKind::AmpAmp:
        return 2;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
        return 3;
    case TokenKind::Less:
    case TokenKind::Greater:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
        return 4;
    case TokenKind::Plus:
    case TokenKind::Minus:
        return 5;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
        return 6;
    default:
        return 0;
    }
}

} // namespace

std::unique_ptr<Expr> Parser::parse_expression() {
    // start at 1 so every binary operator is in play (0 means "not an operator").
    return parse_binary(1);
}

std::unique_ptr<Expr> Parser::parse_binary(int min_prec) {
    auto left = parse_primary();

    while (true) {
        int prec = binary_precedence(peek().kind);
        // either we hit something that isn't a binary operator, or it binds
        // looser than what the caller asked for — either way we're done here.
        if (prec == 0 || prec < min_prec) {
            break;
        }

        Token op = advance();
        // all our operators are left-associative, so the right side stops at the
        // next operator of the same precedence (prec + 1) and hands it back to us
        // to fold in on the left. that's what keeps `1 - 2 - 3` grouping as
        // `(1 - 2) - 3` instead of `1 - (2 - 3)`.
        auto right = parse_binary(prec + 1);
        left = std::make_unique<BinaryExpr>(std::move(left), std::move(op),
                                            std::move(right));
    }

    return left;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    // the literals all carry their value in the lexeme, so we just keep the token
    // around and let a later pass turn it into a real value. true/false/nil are
    // keywords but they read as literals here too.
    if (check(TokenKind::Integer) || check(TokenKind::Float) ||
        check(TokenKind::String) || check(TokenKind::True) ||
        check(TokenKind::False) || check(TokenKind::Nil)) {
        return std::make_unique<LiteralExpr>(advance());
    }

    if (check(TokenKind::Identifier)) {
        return std::make_unique<IdentifierExpr>(advance());
    }

    // a parenthesised expression: drop the parens and keep whatever's inside.
    // we don't make a node for the grouping itself since the tree shape already
    // captures the grouping once binary precedence is in.
    if (match(TokenKind::LParen)) {
        auto inner = parse_expression();
        consume(TokenKind::RParen, "expected ')' after expression");
        return inner;
    }

    throw std::runtime_error("expected an expression");
}

} // namespace brewc
