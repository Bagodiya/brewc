#include "brewc/lexer.h"

namespace brewc {

namespace {
bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}
} // namespace

Lexer::Lexer(std::string_view source)
    : source_(source), pos_(0), line_(1), column_(1) {}

bool Lexer::is_at_end() const {
    return pos_ >= source_.size();
}

char Lexer::peek() const {
    if (is_at_end()) {
        return '\0';
    }
    return source_[pos_];
}

char Lexer::advance() {
    char c = source_[pos_];
    pos_++;
    column_++;
    return c;
}

Token Lexer::next_token() {
    while (!is_at_end() && is_space(peek())) {
        advance();
    }

    if (is_at_end()) {
        return Token(TokenKind::End, "", line_, column_);
    }

    int start_line = line_;
    int start_col = column_;
    std::size_t start = pos_;

    if (is_digit(peek())) {
        while (!is_at_end() && is_digit(peek())) {
            advance();
        }
        std::string text(source_.substr(start, pos_ - start));
        return Token(TokenKind::Integer, std::move(text), start_line, start_col);
    }

    // we don't recognise this char yet, eat it and report an error so the
    // caller doesn't get stuck on the same position forever.
    advance();
    std::string bad(source_.substr(start, 1));
    return Token(TokenKind::Error, std::move(bad), start_line, start_col);
}

} // namespace brewc
