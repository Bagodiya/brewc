#ifndef BREWC_LEXER_H
#define BREWC_LEXER_H

#include <string_view>

#include "brewc/token.h"

namespace brewc {

class Lexer {
public:
    explicit Lexer(std::string_view source);

    // pull the next token off the source. for now this just hands back
    // an End token until the real scanning lands in later steps.
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
