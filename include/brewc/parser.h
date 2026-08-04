#ifndef BREWC_PARSER_H
#define BREWC_PARSER_H

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "brewc/ast.h"
#include "brewc/token.h"

namespace brewc {

// thrown when the parser trips over something it can't make a tree out of. it
// remembers where in the source the trouble was so the driver can point right
// at the offending token instead of just saying "syntax error" somewhere.
// the what() string already has the location baked in, so printing it is enough.
class ParseError : public std::runtime_error {
public:
    ParseError(int line, int column, const std::string& message);

    int line() const { return line_; }
    int column() const { return column_; }

private:
    int line_;
    int column_;
};

// recursive descent parser. it walks the flat list of tokens the lexer produced
// and builds the AST out of it. this step is just the skeleton — the navigation
// helpers below are what every parse_* method (coming in the next steps) leans
// on, so it's worth getting them right first.
class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // parse a whole file: keep pulling statements off the front until the tokens
    // run out. when one statement blows up we stash the error, skip to the next
    // likely statement boundary, and carry on, so a single typo doesn't hide
    // every later mistake. check errors() afterwards to see if anything went
    // wrong — a non-empty list means the returned tree is only the parts we
    // managed to recover.
    std::vector<std::unique_ptr<Stmt>> parse_program();

    // the errors collected during the last parse_program run, in source order.
    const std::vector<ParseError>& errors() const { return errors_; }

    // parse a single statement and hand back the tree for it. the keyword-led
    // forms (let, if, while, fn, return) are picked out by their first token;
    // anything else is treated as a bare expression statement.
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

    // `return` or `return <expr>`. the value is optional, so we only reach for an
    // expression when the next token could actually start one — otherwise a bare
    // `return` at the end of a block would swallow the `}`.
    std::unique_ptr<Stmt> parse_return_stmt();

    // an expression on its own line. this is the fallback in parse_statement, so
    // it's also what reports "expected a statement" when the tokens don't start
    // anything at all.
    std::unique_ptr<Stmt> parse_expr_stmt();

    // a `{ ... }` block. eats the braces and collects whatever statements sit
    // between them. shared by if/while (and later fn) so they all agree on what
    // a block looks like.
    std::unique_ptr<Stmt> parse_block();

    // `x = <expr>`. sits above the binary climber because assignment binds looser
    // than every operator, and it's right-associative so `a = b = 1` nests to the
    // right. we parse the left side as a normal expression first and only then
    // check for an `=`, which means we find out it was a target after the fact —
    // that's why the result has to be an identifier to convert.
    std::unique_ptr<Expr> parse_assignment();

    // precedence climbing. parse a left operand, then keep folding in binary
    // operators as long as they bind at least as tightly as min_prec. operators
    // weaker than min_prec are left for the caller one level up to deal with.
    std::unique_ptr<Expr> parse_binary(int min_prec);

    // prefix operators: `-x` and `!cond`. sits between the binary climber and
    // the primaries so a unary binds tighter than any binary operator but still
    // applies to the whole primary that follows it.
    std::unique_ptr<Expr> parse_unary();

    // postfix call syntax: `foo(1, 2)`. parses a primary first, then keeps
    // wrapping it in a CallExpr for every `(...)` that follows so `f()()` chains
    // fold left. sits just under parse_unary so the call grabs its arguments
    // before a prefix operator gets to the result.
    std::unique_ptr<Expr> parse_call();

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
    // like match but mandatory: the token has to be there. if it isn't, this
    // throws a ParseError pointed at the current token with the given message.
    const Token& consume(TokenKind kind, const std::string& message);

    // build a ParseError that points at `tok`. every throw site funnels through
    // here so the location handling lives in one spot.
    ParseError error_at(const Token& tok, const std::string& message) const;

    // panic-mode recovery. after an error we're sitting at a token we couldn't
    // use, so drop tokens until we're past a `;` or right before a keyword that
    // can only start a fresh statement. that resyncs us to a clean boundary and
    // keeps the next parse_statement from choking on the same garbage.
    void synchronize();

    bool is_at_end() const;

    std::vector<Token> tokens_;
    std::size_t current_;
    std::vector<ParseError> errors_;
};

} // namespace brewc

#endif
