#include "brewc/lexer.h"

namespace brewc {

namespace {
bool is_digit(char c) {
    return c >= '0' && c <= '9';
}

bool is_space(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

bool is_alpha(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_alnum(char c) {
    return is_alpha(c) || is_digit(c);
}

// map a finished word to its keyword token, or Identifier if it's just a name
TokenKind keyword_or_identifier(std::string_view word) {
    if (word == "let") return TokenKind::Let;
    if (word == "fn") return TokenKind::Fn;
    if (word == "if") return TokenKind::If;
    if (word == "else") return TokenKind::Else;
    if (word == "while") return TokenKind::While;
    if (word == "return") return TokenKind::Return;
    if (word == "true") return TokenKind::True;
    if (word == "false") return TokenKind::False;
    if (word == "nil") return TokenKind::Nil;
    return TokenKind::Identifier;
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

char Lexer::peek_next() const {
    if (pos_ + 1 >= source_.size()) {
        return '\0';
    }
    return source_[pos_ + 1];
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

        // only treat the dot as part of a number when there's another digit
        // after it, otherwise "1." should stay an integer and leave the dot.
        bool is_float = false;
        if (peek() == '.' && is_digit(peek_next())) {
            is_float = true;
            advance(); // eat the '.'
            while (!is_at_end() && is_digit(peek())) {
                advance();
            }
        }

        std::string text(source_.substr(start, pos_ - start));
        TokenKind kind = is_float ? TokenKind::Float : TokenKind::Integer;
        return Token(kind, std::move(text), start_line, start_col);
    }

    if (is_alpha(peek())) {
        while (!is_at_end() && is_alnum(peek())) {
            advance();
        }

        std::string_view word = source_.substr(start, pos_ - start);
        TokenKind kind = keyword_or_identifier(word);
        return Token(kind, std::string(word), start_line, start_col);
    }

    // we don't recognise this char yet, eat it and report an error so the
    // caller doesn't get stuck on the same position forever.
    advance();
    std::string bad(source_.substr(start, 1));
    return Token(TokenKind::Error, std::move(bad), start_line, start_col);
}

} // namespace brewc
