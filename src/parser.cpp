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

} // namespace brewc
