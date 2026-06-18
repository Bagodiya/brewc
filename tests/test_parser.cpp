#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "brewc/ast.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/token.h"

using namespace brewc;

namespace {

// run the source through the lexer and collect every token up to and including
// End, so we can hand the parser the same flat list it sees in the real driver.
std::vector<Token> lex_all(const std::string& source) {
    Lexer lexer(source);
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.next_token();
        bool done = tok.kind == TokenKind::End;
        tokens.push_back(std::move(tok));
        if (done) {
            break;
        }
    }
    return tokens;
}

// lex + parse a single expression in one go.
std::unique_ptr<Expr> parse_expr(const std::string& source) {
    Parser parser(lex_all(source));
    return parser.parse_expression();
}

// same shortcut but for a statement, so the let tests below read as one call.
std::unique_ptr<Stmt> parse_stmt(const std::string& source) {
    Parser parser(lex_all(source));
    return parser.parse_statement();
}

} // namespace

TEST_CASE("parses an integer literal", "[parser]") {
    auto expr = parse_expr("42");
    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    REQUIRE(lit != nullptr);
    REQUIRE(lit->token.kind == TokenKind::Integer);
    REQUIRE(lit->token.lexeme == "42");
}

TEST_CASE("parses a float literal", "[parser]") {
    auto expr = parse_expr("3.14");
    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    REQUIRE(lit != nullptr);
    REQUIRE(lit->token.kind == TokenKind::Float);
    REQUIRE(lit->token.lexeme == "3.14");
}

TEST_CASE("parses a string literal", "[parser]") {
    auto expr = parse_expr("\"hello\"");
    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    REQUIRE(lit != nullptr);
    REQUIRE(lit->token.kind == TokenKind::String);
    REQUIRE(lit->token.lexeme == "hello");
}

TEST_CASE("parses the boolean and nil keywords as literals", "[parser]") {
    auto t = parse_expr("true");
    REQUIRE(dynamic_cast<LiteralExpr*>(t.get()) != nullptr);
    REQUIRE(dynamic_cast<LiteralExpr*>(t.get())->token.kind == TokenKind::True);

    auto f = parse_expr("false");
    REQUIRE(dynamic_cast<LiteralExpr*>(f.get()) != nullptr);
    REQUIRE(dynamic_cast<LiteralExpr*>(f.get())->token.kind == TokenKind::False);

    auto n = parse_expr("nil");
    REQUIRE(dynamic_cast<LiteralExpr*>(n.get()) != nullptr);
    REQUIRE(dynamic_cast<LiteralExpr*>(n.get())->token.kind == TokenKind::Nil);
}

TEST_CASE("parses an identifier", "[parser]") {
    auto expr = parse_expr("count");
    auto* id = dynamic_cast<IdentifierExpr*>(expr.get());
    REQUIRE(id != nullptr);
    REQUIRE(id->name == "count");
}

TEST_CASE("a parenthesised expression unwraps to the inner node", "[parser]") {
    auto expr = parse_expr("(7)");
    auto* lit = dynamic_cast<LiteralExpr*>(expr.get());
    REQUIRE(lit != nullptr);
    REQUIRE(lit->token.lexeme == "7");
}

TEST_CASE("nested parens still unwrap to the innermost node", "[parser]") {
    auto expr = parse_expr("(((x)))");
    auto* id = dynamic_cast<IdentifierExpr*>(expr.get());
    REQUIRE(id != nullptr);
    REQUIRE(id->name == "x");
}

TEST_CASE("a missing closing paren is reported", "[parser]") {
    REQUIRE_THROWS(parse_expr("(1"));
}

TEST_CASE("a token that can't start an expression is reported", "[parser]") {
    REQUIRE_THROWS(parse_expr("+"));
}

namespace {

// pull a BinaryExpr out of an Expr* or fail the test if it isn't one. saves
// repeating the cast + null check in every precedence test below.
BinaryExpr* as_binary(Expr* expr) {
    auto* bin = dynamic_cast<BinaryExpr*>(expr);
    REQUIRE(bin != nullptr);
    return bin;
}

// the lexeme of a literal/identifier leaf, so a test can spot-check the operands
// without dragging in the whole token.
std::string leaf_text(Expr* expr) {
    if (auto* lit = dynamic_cast<LiteralExpr*>(expr)) {
        return lit->token.lexeme;
    }
    if (auto* id = dynamic_cast<IdentifierExpr*>(expr)) {
        return id->name;
    }
    return "";
}

} // namespace

TEST_CASE("parses a simple binary expression", "[parser]") {
    auto expr = parse_expr("1 + 2");
    auto* bin = as_binary(expr.get());
    REQUIRE(bin->op.kind == TokenKind::Plus);
    REQUIRE(leaf_text(bin->left.get()) == "1");
    REQUIRE(leaf_text(bin->right.get()) == "2");
}

TEST_CASE("multiplication binds tighter than addition", "[parser]") {
    // should group as 1 + (2 * 3), so the root is the +.
    auto expr = parse_expr("1 + 2 * 3");
    auto* root = as_binary(expr.get());
    REQUIRE(root->op.kind == TokenKind::Plus);
    REQUIRE(leaf_text(root->left.get()) == "1");

    auto* rhs = as_binary(root->right.get());
    REQUIRE(rhs->op.kind == TokenKind::Star);
    REQUIRE(leaf_text(rhs->left.get()) == "2");
    REQUIRE(leaf_text(rhs->right.get()) == "3");
}

TEST_CASE("leading multiplication still nests under the addition", "[parser]") {
    // 2 * 3 + 1 groups as (2 * 3) + 1.
    auto expr = parse_expr("2 * 3 + 1");
    auto* root = as_binary(expr.get());
    REQUIRE(root->op.kind == TokenKind::Plus);
    REQUIRE(leaf_text(root->right.get()) == "1");

    auto* lhs = as_binary(root->left.get());
    REQUIRE(lhs->op.kind == TokenKind::Star);
}

TEST_CASE("subtraction is left associative", "[parser]") {
    // 1 - 2 - 3 must be (1 - 2) - 3, not 1 - (2 - 3).
    auto expr = parse_expr("1 - 2 - 3");
    auto* root = as_binary(expr.get());
    REQUIRE(root->op.kind == TokenKind::Minus);
    REQUIRE(leaf_text(root->right.get()) == "3");

    auto* lhs = as_binary(root->left.get());
    REQUIRE(lhs->op.kind == TokenKind::Minus);
    REQUIRE(leaf_text(lhs->left.get()) == "1");
    REQUIRE(leaf_text(lhs->right.get()) == "2");
}

TEST_CASE("parens override the usual precedence", "[parser]") {
    // (1 + 2) * 3 should put the * at the root with the + tucked underneath.
    auto expr = parse_expr("(1 + 2) * 3");
    auto* root = as_binary(expr.get());
    REQUIRE(root->op.kind == TokenKind::Star);
    REQUIRE(leaf_text(root->right.get()) == "3");

    auto* lhs = as_binary(root->left.get());
    REQUIRE(lhs->op.kind == TokenKind::Plus);
}

TEST_CASE("comparison binds looser than arithmetic", "[parser]") {
    // a + b < c groups as (a + b) < c.
    auto expr = parse_expr("a + b < c");
    auto* root = as_binary(expr.get());
    REQUIRE(root->op.kind == TokenKind::Less);
    REQUIRE(leaf_text(root->right.get()) == "c");

    auto* lhs = as_binary(root->left.get());
    REQUIRE(lhs->op.kind == TokenKind::Plus);
}

TEST_CASE("logical and binds tighter than logical or", "[parser]") {
    // a || b && c groups as a || (b && c).
    auto expr = parse_expr("a || b && c");
    auto* root = as_binary(expr.get());
    REQUIRE(root->op.kind == TokenKind::PipePipe);
    REQUIRE(leaf_text(root->left.get()) == "a");

    auto* rhs = as_binary(root->right.get());
    REQUIRE(rhs->op.kind == TokenKind::AmpAmp);
}

namespace {

// same idea as as_binary but for the unary node, so the tests below don't have
// to repeat the cast and null check.
UnaryExpr* as_unary(Expr* expr) {
    auto* un = dynamic_cast<UnaryExpr*>(expr);
    REQUIRE(un != nullptr);
    return un;
}

} // namespace

TEST_CASE("parses a negation", "[parser]") {
    auto expr = parse_expr("-5");
    auto* un = as_unary(expr.get());
    REQUIRE(un->op.kind == TokenKind::Minus);
    REQUIRE(leaf_text(un->operand.get()) == "5");
}

TEST_CASE("parses a logical not", "[parser]") {
    auto expr = parse_expr("!done");
    auto* un = as_unary(expr.get());
    REQUIRE(un->op.kind == TokenKind::Bang);
    REQUIRE(leaf_text(un->operand.get()) == "done");
}

TEST_CASE("stacked unary operators nest", "[parser]") {
    // !!x is !(!x), so we expect two Bang nodes wrapping the identifier.
    auto expr = parse_expr("!!x");
    auto* outer = as_unary(expr.get());
    REQUIRE(outer->op.kind == TokenKind::Bang);

    auto* inner = as_unary(outer->operand.get());
    REQUIRE(inner->op.kind == TokenKind::Bang);
    REQUIRE(leaf_text(inner->operand.get()) == "x");
}

TEST_CASE("unary binds tighter than multiplication", "[parser]") {
    // -a * b groups as (-a) * b, so the * is at the root with the unary on its
    // left, not -(a * b).
    auto expr = parse_expr("-a * b");
    auto* root = as_binary(expr.get());
    REQUIRE(root->op.kind == TokenKind::Star);
    REQUIRE(leaf_text(root->right.get()) == "b");

    auto* lhs = as_unary(root->left.get());
    REQUIRE(lhs->op.kind == TokenKind::Minus);
    REQUIRE(leaf_text(lhs->operand.get()) == "a");
}

TEST_CASE("unary applies to a parenthesised group", "[parser]") {
    // -(1 + 2) keeps the whole sum under the negation.
    auto expr = parse_expr("-(1 + 2)");
    auto* un = as_unary(expr.get());
    REQUIRE(un->op.kind == TokenKind::Minus);

    auto* inner = as_binary(un->operand.get());
    REQUIRE(inner->op.kind == TokenKind::Plus);
}

TEST_CASE("parses a let with a literal initializer", "[parser]") {
    auto stmt = parse_stmt("let x = 10");
    auto* let = dynamic_cast<LetStmt*>(stmt.get());
    REQUIRE(let != nullptr);
    REQUIRE(let->name == "x");

    auto* lit = dynamic_cast<LiteralExpr*>(let->initializer.get());
    REQUIRE(lit != nullptr);
    REQUIRE(lit->token.lexeme == "10");
}

TEST_CASE("a let initializer can be a whole expression", "[parser]") {
    // the right side should go through the normal expression parser, so the
    // precedence rules still apply: 1 + 2 * 3 hangs a + at the top.
    auto stmt = parse_stmt("let total = 1 + 2 * 3");
    auto* let = dynamic_cast<LetStmt*>(stmt.get());
    REQUIRE(let != nullptr);
    REQUIRE(let->name == "total");

    auto* root = as_binary(let->initializer.get());
    REQUIRE(root->op.kind == TokenKind::Plus);
    REQUIRE(leaf_text(root->left.get()) == "1");
    REQUIRE(as_binary(root->right.get())->op.kind == TokenKind::Star);
}

TEST_CASE("a let initializer can name another variable", "[parser]") {
    auto stmt = parse_stmt("let name = other");
    auto* let = dynamic_cast<LetStmt*>(stmt.get());
    REQUIRE(let != nullptr);
    REQUIRE(let->name == "name");

    auto* id = dynamic_cast<IdentifierExpr*>(let->initializer.get());
    REQUIRE(id != nullptr);
    REQUIRE(id->name == "other");
}

TEST_CASE("a let without a name is reported", "[parser]") {
    REQUIRE_THROWS(parse_stmt("let = 5"));
}

TEST_CASE("a let missing the equals sign is reported", "[parser]") {
    REQUIRE_THROWS(parse_stmt("let x 5"));
}

TEST_CASE("parses an if with no else", "[parser]") {
    auto stmt = parse_stmt("if x < 10 { let y = 1 }");
    auto* iff = dynamic_cast<IfStmt*>(stmt.get());
    REQUIRE(iff != nullptr);

    auto* cond = as_binary(iff->condition.get());
    REQUIRE(cond->op.kind == TokenKind::Less);

    auto* then_b = dynamic_cast<BlockStmt*>(iff->then_branch.get());
    REQUIRE(then_b != nullptr);
    REQUIRE(then_b->statements.size() == 1);
    REQUIRE(iff->else_branch == nullptr);
}

TEST_CASE("parses an if with an else block", "[parser]") {
    auto stmt = parse_stmt("if done { let a = 1 } else { let b = 2 }");
    auto* iff = dynamic_cast<IfStmt*>(stmt.get());
    REQUIRE(iff != nullptr);

    auto* then_b = dynamic_cast<BlockStmt*>(iff->then_branch.get());
    REQUIRE(then_b != nullptr);

    auto* else_b = dynamic_cast<BlockStmt*>(iff->else_branch.get());
    REQUIRE(else_b != nullptr);
    REQUIRE(else_b->statements.size() == 1);
}

TEST_CASE("an else if chains into another if", "[parser]") {
    // the else slot should hold a nested IfStmt, not a block.
    auto stmt = parse_stmt("if a { let x = 1 } else if b { let y = 2 } else { let z = 3 }");
    auto* outer = dynamic_cast<IfStmt*>(stmt.get());
    REQUIRE(outer != nullptr);

    auto* inner = dynamic_cast<IfStmt*>(outer->else_branch.get());
    REQUIRE(inner != nullptr);

    auto* inner_cond = dynamic_cast<IdentifierExpr*>(inner->condition.get());
    REQUIRE(inner_cond != nullptr);
    REQUIRE(inner_cond->name == "b");

    REQUIRE(dynamic_cast<BlockStmt*>(inner->else_branch.get()) != nullptr);
}

TEST_CASE("an empty block is allowed", "[parser]") {
    auto stmt = parse_stmt("if a {}");
    auto* iff = dynamic_cast<IfStmt*>(stmt.get());
    REQUIRE(iff != nullptr);

    auto* then_b = dynamic_cast<BlockStmt*>(iff->then_branch.get());
    REQUIRE(then_b != nullptr);
    REQUIRE(then_b->statements.empty());
}

TEST_CASE("an if missing its opening brace is reported", "[parser]") {
    REQUIRE_THROWS(parse_stmt("if a let x = 1 }"));
}

TEST_CASE("an unterminated block is reported", "[parser]") {
    REQUIRE_THROWS(parse_stmt("if a { let x = 1"));
}

TEST_CASE("parses a while loop", "[parser]") {
    auto stmt = parse_stmt("while i < 10 { let x = i }");
    auto* loop = dynamic_cast<WhileStmt*>(stmt.get());
    REQUIRE(loop != nullptr);

    auto* cond = as_binary(loop->condition.get());
    REQUIRE(cond->op.kind == TokenKind::Less);
    REQUIRE(leaf_text(cond->left.get()) == "i");

    auto* body = dynamic_cast<BlockStmt*>(loop->body.get());
    REQUIRE(body != nullptr);
    REQUIRE(body->statements.size() == 1);
}

TEST_CASE("a while condition goes through the full expression parser", "[parser]") {
    // a && b should hang an && at the root of the condition.
    auto stmt = parse_stmt("while a && b { let x = 1 }");
    auto* loop = dynamic_cast<WhileStmt*>(stmt.get());
    REQUIRE(loop != nullptr);

    auto* cond = as_binary(loop->condition.get());
    REQUIRE(cond->op.kind == TokenKind::AmpAmp);
}

TEST_CASE("a while with an empty body is allowed", "[parser]") {
    auto stmt = parse_stmt("while running {}");
    auto* loop = dynamic_cast<WhileStmt*>(stmt.get());
    REQUIRE(loop != nullptr);

    auto* body = dynamic_cast<BlockStmt*>(loop->body.get());
    REQUIRE(body != nullptr);
    REQUIRE(body->statements.empty());
}

TEST_CASE("a while missing its body brace is reported", "[parser]") {
    REQUIRE_THROWS(parse_stmt("while a let x = 1 }"));
}

TEST_CASE("parses a no-arg function", "[parser]") {
    auto stmt = parse_stmt("fn main() { let x = 1 }");
    auto* fn = dynamic_cast<FnDecl*>(stmt.get());
    REQUIRE(fn != nullptr);
    REQUIRE(fn->name == "main");
    REQUIRE(fn->params.empty());

    auto* body = dynamic_cast<BlockStmt*>(fn->body.get());
    REQUIRE(body != nullptr);
    REQUIRE(body->statements.size() == 1);
}

TEST_CASE("parses a function with one parameter", "[parser]") {
    auto stmt = parse_stmt("fn square(n) { let r = n * n }");
    auto* fn = dynamic_cast<FnDecl*>(stmt.get());
    REQUIRE(fn != nullptr);
    REQUIRE(fn->name == "square");
    REQUIRE(fn->params.size() == 1);
    REQUIRE(fn->params[0].lexeme == "n");
}

TEST_CASE("parses a function with several parameters", "[parser]") {
    auto stmt = parse_stmt("fn add(a, b, c) { let s = a + b }");
    auto* fn = dynamic_cast<FnDecl*>(stmt.get());
    REQUIRE(fn != nullptr);
    REQUIRE(fn->params.size() == 3);
    REQUIRE(fn->params[0].lexeme == "a");
    REQUIRE(fn->params[1].lexeme == "b");
    REQUIRE(fn->params[2].lexeme == "c");
}

TEST_CASE("a function body can be empty", "[parser]") {
    auto stmt = parse_stmt("fn noop() {}");
    auto* fn = dynamic_cast<FnDecl*>(stmt.get());
    REQUIRE(fn != nullptr);

    auto* body = dynamic_cast<BlockStmt*>(fn->body.get());
    REQUIRE(body != nullptr);
    REQUIRE(body->statements.empty());
}

TEST_CASE("a function without a name is reported", "[parser]") {
    REQUIRE_THROWS(parse_stmt("fn () { let x = 1 }"));
}

TEST_CASE("a function missing its parens is reported", "[parser]") {
    REQUIRE_THROWS(parse_stmt("fn main { let x = 1 }"));
}

TEST_CASE("a trailing comma in the parameter list is reported", "[parser]") {
    REQUIRE_THROWS(parse_stmt("fn add(a, b,) { let x = 1 }"));
}

namespace {

// grab a CallExpr off an Expr* or fail the test. mirrors as_binary/as_unary so
// the call tests stay short.
CallExpr* as_call(Expr* expr) {
    auto* call = dynamic_cast<CallExpr*>(expr);
    REQUIRE(call != nullptr);
    return call;
}

} // namespace

TEST_CASE("parses a call with no arguments", "[parser]") {
    auto expr = parse_expr("now()");
    auto* call = as_call(expr.get());
    REQUIRE(call->args.empty());

    auto* callee = dynamic_cast<IdentifierExpr*>(call->callee.get());
    REQUIRE(callee != nullptr);
    REQUIRE(callee->name == "now");
}

TEST_CASE("parses a call with one argument", "[parser]") {
    auto expr = parse_expr("square(5)");
    auto* call = as_call(expr.get());
    REQUIRE(call->args.size() == 1);
    REQUIRE(leaf_text(call->args[0].get()) == "5");
}

TEST_CASE("parses a call with several arguments", "[parser]") {
    auto expr = parse_expr("add(a, b, c)");
    auto* call = as_call(expr.get());
    REQUIRE(call->args.size() == 3);
    REQUIRE(leaf_text(call->args[0].get()) == "a");
    REQUIRE(leaf_text(call->args[1].get()) == "b");
    REQUIRE(leaf_text(call->args[2].get()) == "c");
}

TEST_CASE("an argument can be a whole expression", "[parser]") {
    // the argument should go through the full expression parser, so 1 + 2 hangs
    // a + under the call instead of being split into two arguments.
    auto expr = parse_expr("print(1 + 2)");
    auto* call = as_call(expr.get());
    REQUIRE(call->args.size() == 1);

    auto* arg = as_binary(call->args[0].get());
    REQUIRE(arg->op.kind == TokenKind::Plus);
}

TEST_CASE("a call can be passed as an argument", "[parser]") {
    auto expr = parse_expr("outer(inner(x))");
    auto* outer = as_call(expr.get());
    REQUIRE(outer->args.size() == 1);

    auto* inner = as_call(outer->args[0].get());
    REQUIRE(inner->args.size() == 1);
    REQUIRE(leaf_text(inner->args[0].get()) == "x");
}

TEST_CASE("chained calls nest the earlier one inside", "[parser]") {
    // f()() should put the first call as the callee of the second.
    auto expr = parse_expr("f()()");
    auto* outer = as_call(expr.get());
    REQUIRE(outer->args.empty());

    auto* inner = as_call(outer->callee.get());
    REQUIRE(inner->args.empty());

    auto* base = dynamic_cast<IdentifierExpr*>(inner->callee.get());
    REQUIRE(base != nullptr);
    REQUIRE(base->name == "f");
}

TEST_CASE("a call binds tighter than a prefix operator", "[parser]") {
    // -foo() should negate the result of the call, so the unary sits on top.
    auto expr = parse_expr("-foo()");
    auto* un = as_unary(expr.get());
    REQUIRE(un->op.kind == TokenKind::Minus);
    REQUIRE(dynamic_cast<CallExpr*>(un->operand.get()) != nullptr);
}

TEST_CASE("a call missing its closing paren is reported", "[parser]") {
    REQUIRE_THROWS(parse_expr("foo(1, 2"));
}

TEST_CASE("a trailing comma in an argument list is reported", "[parser]") {
    REQUIRE_THROWS(parse_expr("foo(a,)"));
}

TEST_CASE("a syntax error carries the line and column of the bad token", "[parser]") {
    // the `=` sits where a name should be, at column 5 of the only line.
    try {
        parse_stmt("let = 5");
        FAIL("expected the parser to throw");
    } catch (const ParseError& err) {
        REQUIRE(err.line() == 1);
        REQUIRE(err.column() == 5);
    }
}

TEST_CASE("the error message mentions the location and the reason", "[parser]") {
    try {
        parse_stmt("let x 5");
        FAIL("expected the parser to throw");
    } catch (const ParseError& err) {
        std::string what = err.what();
        REQUIRE(what.find("line 1") != std::string::npos);
        REQUIRE(what.find("'='") != std::string::npos);
    }
}

TEST_CASE("a thrown parse error is a ParseError", "[parser]") {
    REQUIRE_THROWS_AS(parse_expr("(1"), ParseError);
}

TEST_CASE("parse_program recovers and reports every broken statement", "[parser]") {
    // first let is missing its name, the second is fine, the third has no '='.
    // recovery should let us see both errors instead of just the first one.
    Parser parser(lex_all("let = 1 let ok = 2 let y 3"));
    auto program = parser.parse_program();

    REQUIRE(parser.errors().size() == 2);
    // the good statement in the middle should still make it into the tree.
    bool has_let = false;
    for (auto& stmt : program) {
        if (dynamic_cast<LetStmt*>(stmt.get()) != nullptr) {
            has_let = true;
        }
    }
    REQUIRE(has_let);
}

TEST_CASE("parse_program leaves errors empty on clean input", "[parser]") {
    Parser parser(lex_all("let x = 1 let y = 2"));
    auto program = parser.parse_program();

    REQUIRE(parser.errors().empty());
    REQUIRE(program.size() == 2);
}
