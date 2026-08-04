#include "brewc/parser.h"

#include <string>

namespace brewc {

namespace {

// stick the line/column on the front of the message so a printed ParseError
// reads like "line 2:5: expected '=' ..." without the caller doing the work.
std::string with_location(int line, int column, const std::string& message) {
    return "line " + std::to_string(line) + ":" + std::to_string(column) + ": " +
           message;
}

} // namespace

ParseError::ParseError(int line, int column, const std::string& message)
    : std::runtime_error(with_location(line, column, message)),
      line_(line), column_(column) {}

Parser::Parser(std::vector<Token> tokens)
    : tokens_(std::move(tokens)), current_(0) {}

bool Parser::is_at_end() const {
    return peek().kind == TokenKind::End;
}

const Token& Parser::peek() const {
    return tokens_[current_];
}

const Token& Parser::previous() const {
    return tokens_[current_ - 1];
}

const Token& Parser::advance() {
    if (!is_at_end()) {
        current_++;
    }
    return previous();
}

bool Parser::check(TokenKind kind) const {
    if (is_at_end()) {
        return false;
    }
    return peek().kind == kind;
}

bool Parser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

const Token& Parser::consume(TokenKind kind, const std::string& message) {
    if (check(kind)) {
        return advance();
    }
    // point the error at whatever we actually found, not where we wish we were.
    throw error_at(peek(), message);
}

ParseError Parser::error_at(const Token& tok, const std::string& message) const {
    return ParseError(tok.line, tok.column, message);
}

void Parser::synchronize() {
    advance(); // step over the token that tripped us up in the first place.

    while (!is_at_end()) {
        // a semicolon closes off a statement, so the next token is a clean start.
        if (previous().kind == TokenKind::Semicolon) {
            return;
        }
        // these keywords can only ever begin a new statement, so seeing one means
        // we've found our footing again.
        switch (peek().kind) {
        case TokenKind::Let:
        case TokenKind::Fn:
        case TokenKind::If:
        case TokenKind::While:
        case TokenKind::Return:
            return;
        default:
            advance();
        }
    }
}

namespace {

// how tightly each binary operator binds. higher number wins, so it grabs its
// operands before a weaker neighbour does. anything that isn't a binary operator
// returns 0, which the climbing loop reads as "stop, this isn't ours".
int binary_precedence(TokenKind kind) {
    switch (kind) {
    case TokenKind::PipePipe:
        return 1;
    case TokenKind::AmpAmp:
        return 2;
    case TokenKind::EqualEqual:
    case TokenKind::BangEqual:
        return 3;
    case TokenKind::Less:
    case TokenKind::Greater:
    case TokenKind::LessEqual:
    case TokenKind::GreaterEqual:
        return 4;
    case TokenKind::Plus:
    case TokenKind::Minus:
        return 5;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
        return 6;
    default:
        return 0;
    }
}

} // namespace

std::vector<std::unique_ptr<Stmt>> Parser::parse_program() {
    errors_.clear();
    std::vector<std::unique_ptr<Stmt>> statements;

    while (!is_at_end()) {
        try {
            statements.push_back(parse_statement());
        } catch (const ParseError& err) {
            // don't bail on the first mistake, record it and skip ahead so we
            // can still report whatever else is broken further down the file.
            errors_.push_back(err);
            synchronize();
        }
    }

    return statements;
}

std::unique_ptr<Stmt> Parser::parse_statement() {
    if (check(TokenKind::Let)) {
        return parse_let_stmt();
    }
    if (check(TokenKind::If)) {
        return parse_if_stmt();
    }
    if (check(TokenKind::While)) {
        return parse_while_stmt();
    }
    if (check(TokenKind::Fn)) {
        return parse_fn_decl();
    }
    if (check(TokenKind::Return)) {
        return parse_return_stmt();
    }
    // a block on its own, not hanging off an if or a while. it's worth having on
    // its own because it opens a scope, so it's how you keep a `let` from leaking
    // into the rest of the program.
    if (check(TokenKind::LBrace)) {
        return parse_block();
    }
    // no keyword claimed it, so the only thing left it can be is an expression
    // standing on its own. parse_expr_stmt is where "expected an expression"
    // comes from when it isn't one either.
    return parse_expr_stmt();
}

std::unique_ptr<Stmt> Parser::parse_return_stmt() {
    Token keyword = advance(); // the `return` parse_statement peeked at.

    // a bare `return` is allowed, so only look for a value when there's something
    // in front of us that could start one. a `}` means we're at the end of the
    // block and a `;` closes the statement off, and neither begins an expression.
    std::unique_ptr<Expr> value;
    if (!check(TokenKind::RBrace) && !check(TokenKind::Semicolon) && !is_at_end()) {
        value = parse_expression();
    }

    match(TokenKind::Semicolon); // optional, same as everywhere else.
    return std::make_unique<ReturnStmt>(std::move(keyword), std::move(value));
}

std::unique_ptr<Stmt> Parser::parse_expr_stmt() {
    auto expr = parse_expression();
    match(TokenKind::Semicolon); // semicolons are optional, eat one if it's there.
    return std::make_unique<ExprStmt>(std::move(expr));
}

std::unique_ptr<Stmt> Parser::parse_let_stmt() {
    advance(); // drop the `let` we already peeked at in parse_statement.
    const Token& name = consume(TokenKind::Identifier,
                                "expected a name after 'let'");
    consume(TokenKind::Equal, "expected '=' after the name in a let");
    auto initializer = parse_expression();
    match(TokenKind::Semicolon); // optional, so `let x = 1;` reads fine too.
    return std::make_unique<LetStmt>(name, std::move(initializer));
}

std::unique_ptr<Stmt> Parser::parse_if_stmt() {
    advance(); // drop the `if` parse_statement already peeked at.
    auto condition = parse_expression();
    auto then_branch = parse_block();

    std::unique_ptr<Stmt> else_branch;
    if (match(TokenKind::Else)) {
        // `else if ...` is just another if hanging off the else slot, so let the
        // recursion handle it. a plain `else` is a block like the then side.
        if (check(TokenKind::If)) {
            else_branch = parse_if_stmt();
        } else {
            else_branch = parse_block();
        }
    }

    return std::make_unique<IfStmt>(std::move(condition), std::move(then_branch),
                                    std::move(else_branch));
}

std::unique_ptr<Stmt> Parser::parse_while_stmt() {
    advance(); // drop the `while` parse_statement peeked at.
    auto condition = parse_expression();
    auto body = parse_block();
    return std::make_unique<WhileStmt>(std::move(condition), std::move(body));
}

std::unique_ptr<Stmt> Parser::parse_fn_decl() {
    advance(); // drop the `fn` parse_statement peeked at.
    const Token& name = consume(TokenKind::Identifier,
                                "expected a name after 'fn'");

    consume(TokenKind::LParen, "expected '(' after the function name");
    std::vector<Token> params;
    // an empty list (`fn main()`) is fine, so only read names while we're not
    // already sitting on the closing paren.
    if (!check(TokenKind::RParen)) {
        do {
            params.push_back(consume(TokenKind::Identifier,
                                     "expected a parameter name"));
        } while (match(TokenKind::Comma));
    }
    consume(TokenKind::RParen, "expected ')' after the parameter list");

    auto body = parse_block();
    return std::make_unique<FnDecl>(name, std::move(params), std::move(body));
}

std::unique_ptr<Stmt> Parser::parse_block() {
    consume(TokenKind::LBrace, "expected '{' to start a block");
    std::vector<std::unique_ptr<Stmt>> statements;
    while (!check(TokenKind::RBrace) && !is_at_end()) {
        statements.push_back(parse_statement());
    }
    consume(TokenKind::RBrace, "expected '}' to close the block");
    return std::make_unique<BlockStmt>(std::move(statements));
}

std::unique_ptr<Expr> Parser::parse_expression() {
    return parse_assignment();
}

std::unique_ptr<Expr> Parser::parse_assignment() {
    // parse the left side as an ordinary expression first. we can't tell an
    // assignment from a plain expression by looking at the first token — `x` and
    // `x = 1` start the same way — so we let the operators run and check for an
    // `=` afterwards. start at 1 so every binary operator is in play (0 means
    // "not an operator").
    auto left = parse_binary(1);

    if (check(TokenKind::Equal)) {
        Token equals = advance();
        // recurse rather than loop, which is what makes `a = b = 1` group to the
        // right: the whole `b = 1` becomes the value assigned to a.
        auto value = parse_assignment();

        // only a bare name is a legal target. we already built a node for the
        // left side, so reach into it for the token instead of re-parsing.
        if (auto* target = dynamic_cast<IdentifierExpr*>(left.get())) {
            return std::make_unique<AssignExpr>(target->token, std::move(value));
        }
        // point at the `=` rather than at the left side: the expression itself is
        // fine, it's using it as a target that isn't.
        throw error_at(equals, "cannot assign to this expression");
    }

    return left;
}

std::unique_ptr<Expr> Parser::parse_binary(int min_prec) {
    auto left = parse_unary();

    while (true) {
        int prec = binary_precedence(peek().kind);
        // either we hit something that isn't a binary operator, or it binds
        // looser than what the caller asked for. either way we're done here.
        if (prec == 0 || prec < min_prec) {
            break;
        }

        Token op = advance();
        // all our operators are left-associative, so the right side stops at the
        // next operator of the same precedence (prec + 1) and hands it back to us
        // to fold in on the left. that's what keeps `1 - 2 - 3` grouping as
        // `(1 - 2) - 3` instead of `1 - (2 - 3)`.
        auto right = parse_binary(prec + 1);
        left = std::make_unique<BinaryExpr>(std::move(left), std::move(op),
                                            std::move(right));
    }

    return left;
}

std::unique_ptr<Expr> Parser::parse_unary() {
    // `-` and `!` are the only prefix operators. recurse on the operand so a
    // stack like `--x` or `!!done` just nests one unary node inside another.
    if (check(TokenKind::Minus) || check(TokenKind::Bang)) {
        Token op = advance();
        auto operand = parse_unary();
        return std::make_unique<UnaryExpr>(std::move(op), std::move(operand));
    }
    return parse_call();
}

std::unique_ptr<Expr> Parser::parse_call() {
    auto expr = parse_primary();

    // keep eating `(...)` groups so a chain like `f()()` ends up nested with the
    // earlier call on the inside. one iteration handles one set of parens.
    while (check(TokenKind::LParen)) {
        Token paren = advance();

        std::vector<std::unique_ptr<Expr>> args;
        // `f()` has no arguments, so only read any if we're not already at the
        // closing paren. each argument is a full expression, which is what lets
        // calls show up as arguments to other calls.
        if (!check(TokenKind::RParen)) {
            do {
                args.push_back(parse_expression());
            } while (match(TokenKind::Comma));
        }
        consume(TokenKind::RParen, "expected ')' after the argument list");

        expr = std::make_unique<CallExpr>(std::move(expr), std::move(paren),
                                          std::move(args));
    }

    return expr;
}

std::unique_ptr<Expr> Parser::parse_primary() {
    // the literals all carry their value in the lexeme, so we just keep the token
    // around and let a later pass turn it into a real value. true/false/nil are
    // keywords but they read as literals here too.
    if (check(TokenKind::Integer) || check(TokenKind::Float) ||
        check(TokenKind::String) || check(TokenKind::True) ||
        check(TokenKind::False) || check(TokenKind::Nil)) {
        return std::make_unique<LiteralExpr>(advance());
    }

    if (check(TokenKind::Identifier)) {
        return std::make_unique<IdentifierExpr>(advance());
    }

    // a parenthesised expression: drop the parens and keep whatever's inside.
    // we don't make a node for the grouping itself since the tree shape already
    // captures the grouping once binary precedence is in.
    if (match(TokenKind::LParen)) {
        auto inner = parse_expression();
        consume(TokenKind::RParen, "expected ')' after expression");
        return inner;
    }

    throw error_at(peek(), "expected an expression");
}

} // namespace brewc
