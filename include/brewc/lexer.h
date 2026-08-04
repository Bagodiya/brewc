#ifndef BREWC_LEXER_H
#define BREWC_LEXER_H

#include <string_view>

#include "brewc/token.h"

namespace brewc {

class Lexer {
public:
    explicit Lexer(std::string_view source);

    // pull the next token off the source, skipping any whitespace in front of it.
    // once the source runs out this keeps handing back End, so a caller can loop
    // on it without checking the position itself. a character that can't start
    // any token comes back as an Error token rather than throwing, which keeps
    // the cursor moving and lets the parser report it in context.
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
