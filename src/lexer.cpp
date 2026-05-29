#include "brewc/lexer.h"

namespace brewc {

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
    // nothing scans yet, so every call reports end of input.
    return Token(TokenKind::End, "", line_, column_);
}

} // namespace brewc
