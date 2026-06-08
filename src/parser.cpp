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

std::unique_ptr<Expr> Parser::parse_expression() {
    // nothing above primary exists yet — the precedence parser slots in here next.
    return parse_primary();
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
