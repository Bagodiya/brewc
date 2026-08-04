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
    // a newline moves us down a line and back to the first column, every
    // other char just bumps the column over by one
    if (c == '\n') {
        line_++;
        column_ = 1;
    } else {
        column_++;
    }
    return c;
}

Token Lexer::next_token() {
    // skip anything that isn't part of a token. whitespace and comments can
    // alternate any number of times — a comment is usually followed by the
    // newline that ends it and then the indent of the next line — so this keeps
    // going until what's in front of us is neither.
    while (!is_at_end()) {
        if (is_space(peek())) {
            advance();
            continue;
        }
        // `//` runs to the end of the line. the newline itself is left alone for
        // the whitespace branch above to eat, which is what keeps advance() in
        // charge of counting lines instead of duplicating that here.
        if (peek() == '/' && peek_next() == '/') {
            while (!is_at_end() && peek() != '\n') {
                advance();
            }
            continue;
        }
        break;
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

    if (peek() == '"') {
        advance(); // skip the opening quote

        // build up the actual text as we go so the lexeme we hand back is the
        // real string value, not the raw source with the backslashes still in
        std::string value;
        while (!is_at_end() && peek() != '"') {
            char ch = advance();
            if (ch == '\\' && !is_at_end()) {
                char esc = advance();
                switch (esc) {
                case 'n': value.push_back('\n'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                default: value.push_back(esc); break; // leave unknown escapes alone
                }
            } else {
                value.push_back(ch);
            }
        }

        // if the loop stopped because we ran out of source instead of hitting
        // a closing quote, the string was never finished. hand back an error
        // so the caller knows something is wrong instead of a half-read string.
        if (is_at_end()) {
            return Token(TokenKind::Error, "unterminated string", start_line, start_col);
        }

        advance(); // eat the closing quote
        return Token(TokenKind::String, std::move(value), start_line, start_col);
    }

    // operators and punctuation. some of these can be two chars long (== <=
    // && ...), so after grabbing the first char we look at the next one and
    // glue them together when it matches.
    char c = advance();
    auto make = [&](TokenKind kind) {
        std::size_t len = pos_ - start;
        return Token(kind, std::string(source_.substr(start, len)), start_line, start_col);
    };

    switch (c) {
    case '+': return make(TokenKind::Plus);
    case '-': return make(TokenKind::Minus);
    case '*': return make(TokenKind::Star);
    case '/': return make(TokenKind::Slash);
    case '%': return make(TokenKind::Percent);
    case '(': return make(TokenKind::LParen);
    case ')': return make(TokenKind::RParen);
    case '{': return make(TokenKind::LBrace);
    case '}': return make(TokenKind::RBrace);
    case ',': return make(TokenKind::Comma);
    case ';': return make(TokenKind::Semicolon);
    case '=':
        if (peek() == '=') {
            advance();
            return make(TokenKind::EqualEqual);
        }
        return make(TokenKind::Equal);
    case '!':
        if (peek() == '=') {
            advance();
            return make(TokenKind::BangEqual);
        }
        return make(TokenKind::Bang);
    case '<':
        if (peek() == '=') {
            advance();
            return make(TokenKind::LessEqual);
        }
        return make(TokenKind::Less);
    case '>':
        if (peek() == '=') {
            advance();
            return make(TokenKind::GreaterEqual);
        }
        return make(TokenKind::Greater);
    case '&':
        // only && is valid, a lone & falls through to the error below
        if (peek() == '&') {
            advance();
            return make(TokenKind::AmpAmp);
        }
        break;
    case '|':
        if (peek() == '|') {
            advance();
            return make(TokenKind::PipePipe);
        }
        break;
    default:
        break;
    }

    // nothing matched, hand back an error token for this char so the caller
    // doesn't get stuck on the same position forever. the lexeme carries the
    // complaint rather than the character itself, the same way the unterminated
    // string above does — see the note on next_token in the header.
    std::string bad(source_.substr(start, 1));
    return Token(TokenKind::Error, "unexpected character '" + bad + "'", start_line,
                 start_col);
}

} // namespace brewc
