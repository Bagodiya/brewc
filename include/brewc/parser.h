#ifndef BREWC_PARSER_H
#define BREWC_PARSER_H

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "brewc/ast.h"
#include "brewc/token.h"

namespace brewc {

// recursive descent parser. it walks the flat list of tokens the lexer produced
// and builds the AST out of it. this step is just the skeleton — the navigation
// helpers below are what every parse_* method (coming in the next steps) leans
// on, so it's worth getting them right first.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // parse a single expression and hand back the tree for it. for now this is
    // just the primary parser; once precedence climbing lands (next step) this
    // becomes the real entry point and parse_primary drops back to a leaf parser.
    std::unique_ptr<Expr> parse_expression();

private:
    // the smallest pieces: literals, names, and a parenthesised expression.
    std::unique_ptr<Expr> parse_primary();

    // the token under the cursor, without moving past it.
    const Token& peek() const;
    // the token we just consumed. handy after advance()/match().
    const Token& previous() const;
    // consume the current token and hand it back, stepping the cursor forward.
    const Token& advance();

    // true if the current token is of the given kind. doesn't consume anything
    // and never runs off the end (End just keeps comparing false here).
    bool check(TokenKind kind) const;
    // if the current token matches, eat it and return true; otherwise leave the
    // cursor alone and return false. this is the workhorse for optional bits.
    bool match(TokenKind kind);
    // like match but mandatory: the token has to be there. for now a mismatch
    // throws — proper error reporting with locations comes in a later step.
    const Token& consume(TokenKind kind, const std::string& message);

    bool is_at_end() const;

    std::vector<Token> tokens_;
    std::size_t current_;
};

} // namespace brewc

#endif
