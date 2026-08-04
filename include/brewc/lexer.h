#ifndef BREWC_LEXER_H
#define BREWC_LEXER_H

#include <string_view>

#include "brewc/token.h"

namespace brewc {

class Lexer {
public:
    explicit Lexer(std::string_view source);

    // pull the next token off the source, skipping any whitespace and comments in
    // front of it. once the source runs out this keeps handing back End, so a
    // caller can loop on it without checking the position itself.
    //
    // anything the lexer can't make a token out of comes back as an Error token
    // rather than throwing, which keeps the cursor moving so one bad character
    // doesn't hide the rest of the file. an Error token is the one case where
    // `lexeme` holds a message ("unterminated string") instead of the source text
    // it was cut from — the lexer already knows what went wrong, and the parser
    // reports that message rather than guessing from context.
    Token next_token();

private:
    char peek() const;
    char peek_next() const;
    char advance();
    bool is_at_end() const;

    std::string_view source_;
    std::size_t pos_;
    int line_;
    int column_;
};

} // namespace brewc

#endif
