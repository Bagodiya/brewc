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

    // parse a single statement and hand back the tree for it. right now the only
    // statement we know about is `let`, but this is the spot every other kind
    // (if, while, fn, ...) will get hooked into as the later steps land.
    std::unique_ptr<Stmt> parse_statement();

    // parse a single expression and hand back the tree for it. this is the real
    // entry point now: it kicks off precedence climbing at the lowest level so
    // the whole operator grammar gets handled, not just leaves.
    std::unique_ptr<Expr> parse_expression();

private:
    // `let x = <expr>`. assumes the caller already saw a `let` up next and lets
    // parse_statement route us here. grabs the name, eats the `=`, then reuses
    // the expression parser for whatever the initializer turns out to be.
    std::unique_ptr<Stmt> parse_let_stmt();

    // `if <cond> { ... } else { ... }`. the else side is optional, and when it's
    // followed by another `if` we just parse that as the else branch so `else if`
    // chains fall out for free.
    std::unique_ptr<Stmt> parse_if_stmt();

    // `while <cond> { ... }`. eats the keyword, reads the condition, then runs
    // the body through parse_block just like the if branches do.
    std::unique_ptr<Stmt> parse_while_stmt();

    // `fn name(a, b) { ... }`. eats the keyword and the name, reads the comma
    // separated parameter list inside the parens (which can be empty), then the
    // body comes through parse_block so it lines up with the other blocks.
    std::unique_ptr<Stmt> parse_fn_decl();

    // a `{ ... }` block. eats the braces and collects whatever statements sit
    // between them. shared by if/while (and later fn) so they all agree on what
    // a block looks like.
    std::unique_ptr<Stmt> parse_block();

    // precedence climbing. parse a left operand, then keep folding in binary
    // operators as long as they bind at least as tightly as min_prec. operators
    // weaker than min_prec are left for the caller one level up to deal with.
    std::unique_ptr<Expr> parse_binary(int min_prec);

    // prefix operators: `-x` and `!cond`. sits between the binary climber and
    // the primaries so a unary binds tighter than any binary operator but still
    // applies to the whole primary that follows it.
    std::unique_ptr<Expr> parse_unary();

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
