#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "brewc/ast.h"
#include "brewc/chunk.h"
#include "brewc/compiler.h"
#include "brewc/disassembler.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/token.h"
#include "brewc/vm.h"

using namespace brewc;

namespace {

// the same lex-everything helper the parser and interpreter tests use, so the
// compiler is fed the exact token list the real driver would give it.
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

// parse and insist it worked. the parser recovers from errors instead of
// throwing, so a typo in a test source string would otherwise hand back an empty
// program that compiles to a bare Return — which is exactly what half the checks
// below expect, so the test would pass without compiling anything.
std::vector<std::unique_ptr<Stmt>> parse_program(const std::string& source) {
    Parser parser(lex_all(source));
    std::vector<std::unique_ptr<Stmt>> program = parser.parse_program();
    REQUIRE(parser.errors().empty());
    return program;
}

// source straight through to bytecode, which is what most tests here want.
Chunk compile_source(const std::string& source) {
    std::vector<std::unique_ptr<Stmt>> program = parse_program(source);
    Compiler compiler;
    return compiler.compile(program);
}

// read one byte back as an opcode. the chunk stores plain bytes, so a test
// comparing against Opcode::Return has to cast somewhere.
Opcode op_at(const Chunk& chunk, std::size_t offset) {
    return static_cast<Opcode>(chunk.code[offset]);
}

// compile a lone literal token. a bare `42` does parse as a statement now, but
// going through the parser gives whatever line and column the source happens to
// put it at, and some of the checks below are about exactly those. building the
// node by hand is the only way to pick them.
Chunk compile_literal(TokenKind kind, const std::string& text, int line = 1, int column = 1) {
    Compiler compiler;
    LiteralExpr literal(Token(kind, text, line, column));
    return compiler.compile_expression(literal);
}

// compile one expression written as source. going through parse_expression means
// the operators get their real precedence instead of whatever shape a hand-built
// tree happens to have, which is half of what the binary tests are checking.
Chunk compile_expr_source(const std::string& source) {
    Parser parser(lex_all(source));
    std::unique_ptr<Expr> expr = parser.parse_expression();
    REQUIRE(parser.errors().empty());
    REQUIRE(expr != nullptr);

    Compiler compiler;
    return compiler.compile_expression(*expr);
}

// how many bytes the instruction at this offset takes up. a test that wants to
// walk the whole stream has to know, since an operand byte read as an opcode is
// how a scan starts reporting instructions nobody emitted.
std::size_t instruction_size(const Chunk& chunk, std::size_t offset) {
    switch (static_cast<Opcode>(chunk.code[offset])) {
    case Opcode::Const:
    case Opcode::DefineGlobal:
    case Opcode::GetGlobal:
    case Opcode::SetGlobal:
    case Opcode::GetLocal:
    case Opcode::SetLocal:
    case Opcode::Call:
        return 2;
    case Opcode::Jump:
    case Opcode::JumpIfFalse:
    case Opcode::Loop:
        return 3;
    default:
        return 1;
    }
}

// compile a whole program and run it. everything else in this file reads the
// bytes back, but a Pop is only worth anything for what the stack looks like when
// the program finishes, and the chunk on its own can't show that.
void run_source(VM& vm, const std::string& source) {
    Chunk chunk = compile_source(source);
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
}

// the function a Const at this offset pushes. fails the test instead of throwing
// bad_variant_access if something else ended up in that pool slot.
std::shared_ptr<CompiledFn> fn_at(const Chunk& chunk, std::size_t offset) {
    REQUIRE(op_at(chunk, offset) == Opcode::Const);
    const Value& value = chunk.constant_at(chunk.code[offset + 1]);
    REQUIRE(is_compiled_fn(value));
    return std::get<std::shared_ptr<CompiledFn>>(value);
}

} // namespace

TEST_CASE("an empty program compiles to a single Return", "[compiler]") {
    Chunk chunk = compile_source("");
    REQUIRE(chunk.size() == 1);
    REQUIRE(op_at(chunk, 0) == Opcode::Return);
}

TEST_CASE("the trailing Return gets a line recorded for it", "[compiler]") {
    // a runtime error on the last instruction still has to report somewhere, so
    // no byte is allowed into the chunk without a line beside it.
    Chunk chunk = compile_source("");
    REQUIRE(chunk.lines.size() == chunk.code.size());
    REQUIRE(chunk.line_at(0) == 1);
}

TEST_CASE("a fresh chunk starts with an empty constant pool", "[compiler]") {
    Chunk chunk = compile_source("");
    REQUIRE(chunk.constants.empty());
}

TEST_CASE("statements compile without the stubs blowing up", "[compiler]") {
    // return is the last empty visitor. call used to be the other example here
    // and compiles for real now, it's down with the call tests.
    REQUIRE_NOTHROW(compile_source("fn add(a, b) { return a + b }"));
}

TEST_CASE("nested blocks are walked all the way down", "[compiler]") {
    // the top level is just Const, DefineGlobal, Return. everything else ended up
    // in the function's own chunk, so that's where the loop has to be.
    Chunk chunk = compile_source("fn outer(n) { while n { if n { let deep = 1 } } }");
    REQUIRE(chunk.size() == 5);
    REQUIRE(op_at(chunk, 0) == Opcode::Const);
    REQUIRE(op_at(chunk, 2) == Opcode::DefineGlobal);
    REQUIRE(op_at(chunk, 4) == Opcode::Return);

    const Chunk& body = fn_at(chunk, 0)->chunk;
    bool has_loop = false;
    for (std::size_t i = 0; i < body.size(); i += instruction_size(body, i)) {
        if (op_at(body, i) == Opcode::Loop) has_loop = true;
    }
    REQUIRE(has_loop);
}

TEST_CASE("a single expression can be compiled on its own", "[compiler]") {
    // Const, its pool index, then the Return compile_expression puts on the end.
    Chunk chunk = compile_literal(TokenKind::Integer, "42");
    REQUIRE(chunk.size() == 3);
    REQUIRE(op_at(chunk, 0) == Opcode::Const);
    REQUIRE(op_at(chunk, 2) == Opcode::Return);
}

TEST_CASE("an integer literal lands in the pool with Const pointing at it", "[compiler]") {
    Chunk chunk = compile_literal(TokenKind::Integer, "42");
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(is_int(chunk.constants[0]));
    REQUIRE(std::get<int64_t>(chunk.constants[0]) == 42);

    // the byte after Const is the index, and this is the only constant so far.
    REQUIRE(chunk.code[1] == 0);
}

TEST_CASE("a float literal keeps its fractional part", "[compiler]") {
    Chunk chunk = compile_literal(TokenKind::Float, "3.5");
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(is_float(chunk.constants[0]));
    REQUIRE(std::get<double>(chunk.constants[0]) == 3.5);
}

TEST_CASE("a string literal is stored as a string constant", "[compiler]") {
    // the lexer already stripped the quotes and expanded the escapes, so what the
    // token carries is the finished text.
    Chunk chunk = compile_literal(TokenKind::String, "hello");
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(is_string(chunk.constants[0]));
    REQUIRE(std::get<std::string>(chunk.constants[0]) == "hello");
}

TEST_CASE("true, false and nil use their own opcode and no pool slot", "[compiler]") {
    Chunk yes = compile_literal(TokenKind::True, "true");
    REQUIRE(yes.size() == 2);
    REQUIRE(op_at(yes, 0) == Opcode::True);
    REQUIRE(yes.constants.empty());

    Chunk no = compile_literal(TokenKind::False, "false");
    REQUIRE(op_at(no, 0) == Opcode::False);
    REQUIRE(no.constants.empty());

    Chunk nothing = compile_literal(TokenKind::Nil, "nil");
    REQUIRE(op_at(nothing, 0) == Opcode::Nil);
    REQUIRE(nothing.constants.empty());
}

TEST_CASE("a literal stamps its own source line on the instruction", "[compiler]") {
    Chunk chunk = compile_literal(TokenKind::Integer, "7", 9, 4);
    REQUIRE(chunk.line_at(0) == 9);
    REQUIRE(chunk.line_at(1) == 9);
}

TEST_CASE("the same literal compiled twice does not double up in the pool", "[compiler]") {
    // add_constant already dedupes, but the compiler has to be handing it equal
    // values for that to help — pushing the token text through unconverted would
    // still be one slot per literal.
    Compiler compiler;
    LiteralExpr first(Token(TokenKind::Integer, "5", 1, 1));
    LiteralExpr second(Token(TokenKind::Integer, "5", 2, 1));

    compiler.compile_expression(first);
    Chunk chunk = compiler.compile_expression(second);

    // a fresh chunk each time, so both runs should look identical.
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(std::get<int64_t>(chunk.constants[0]) == 5);
}

TEST_CASE("the same compiler can run twice without carrying bytes over", "[compiler]") {
    // the chunk is moved out at the end of compile(), so the second run has to
    // start from a fresh one rather than whatever the move left behind.
    Compiler compiler;
    std::vector<std::unique_ptr<Stmt>> first = parse_program("let a = 1");
    std::vector<std::unique_ptr<Stmt>> second = parse_program("let b = 2");

    Chunk one = compiler.compile(first);
    Chunk two = compiler.compile(second);

    REQUIRE(one.size() == two.size());

    // the constant pool is where a leftover would show. the second chunk needs
    // 2 and "b" and nothing else, so anything carried over from the first run
    // would put four entries in here instead of two.
    REQUIRE(two.constants.size() == 2);
    REQUIRE(std::get<std::string>(two.constants[1]) == "b");
}

TEST_CASE("an addition compiles to both operands then Add", "[compiler]") {
    // Const 0, Const 1, Add, Return — the operator comes last because it works off
    // what the operands already left on the stack.
    Chunk chunk = compile_expr_source("1 + 2");
    REQUIRE(chunk.size() == 6);
    REQUIRE(op_at(chunk, 0) == Opcode::Const);
    REQUIRE(op_at(chunk, 2) == Opcode::Const);
    REQUIRE(op_at(chunk, 4) == Opcode::Add);
    REQUIRE(op_at(chunk, 5) == Opcode::Return);
}

TEST_CASE("the left operand is pushed before the right one", "[compiler]") {
    // this is what keeps subtraction from coming out backwards. both constants are
    // in the pool in the order they were compiled, so the indices in the stream say
    // which side went first.
    Chunk chunk = compile_expr_source("7 - 3");
    REQUIRE(std::get<int64_t>(chunk.constants[chunk.code[1]]) == 7);
    REQUIRE(std::get<int64_t>(chunk.constants[chunk.code[3]]) == 3);
    REQUIRE(op_at(chunk, 4) == Opcode::Sub);
}

TEST_CASE("each arithmetic operator gets its own opcode", "[compiler]") {
    REQUIRE(op_at(compile_expr_source("1 + 2"), 4) == Opcode::Add);
    REQUIRE(op_at(compile_expr_source("1 - 2"), 4) == Opcode::Sub);
    REQUIRE(op_at(compile_expr_source("1 * 2"), 4) == Opcode::Mul);
    REQUIRE(op_at(compile_expr_source("1 / 2"), 4) == Opcode::Div);
    REQUIRE(op_at(compile_expr_source("1 % 2"), 4) == Opcode::Mod);
}

TEST_CASE("precedence decides which opcode is emitted first", "[compiler]") {
    // 1 + 2 * 3 — the multiply is the right child, so it has to be finished and
    // sitting on the stack before the Add runs.
    Chunk chunk = compile_expr_source("1 + 2 * 3");
    REQUIRE(chunk.size() == 9);
    REQUIRE(op_at(chunk, 6) == Opcode::Mul);
    REQUIRE(op_at(chunk, 7) == Opcode::Add);
}

TEST_CASE("parens override precedence in the bytecode too", "[compiler]") {
    Chunk chunk = compile_expr_source("(1 + 2) * 3");
    REQUIRE(op_at(chunk, 4) == Opcode::Add);
    REQUIRE(op_at(chunk, 7) == Opcode::Mul);
}

TEST_CASE("a left-nested chain compiles left to right", "[compiler]") {
    // 1 - 2 - 3 groups as (1 - 2) - 3, so the first Sub lands in the middle of the
    // stream rather than both of them ending up at the end.
    Chunk chunk = compile_expr_source("1 - 2 - 3");
    REQUIRE(op_at(chunk, 4) == Opcode::Sub);
    REQUIRE(op_at(chunk, 7) == Opcode::Sub);
}

TEST_CASE("string operands go through the same Add", "[compiler]") {
    // concatenation is a runtime decision about the values, not a separate
    // instruction, so the compiler treats it like any other addition.
    Chunk chunk = compile_expr_source("\"he\" + \"llo\"");
    REQUIRE(op_at(chunk, 4) == Opcode::Add);
    REQUIRE(chunk.constants.size() == 2);
    REQUIRE(is_string(chunk.constants[0]));
}

TEST_CASE("the operator instruction is stamped with the operator's line", "[compiler]") {
    // the operands are compiled first and move line_ along with them, so without
    // setting it back the Add would claim to come from line 3.
    Chunk chunk = compile_expr_source("1 +\n2");
    REQUIRE(chunk.line_at(2) == 2);
    REQUIRE(chunk.line_at(4) == 1);
}

TEST_CASE("a repeated operand is only stored once", "[compiler]") {
    // both sides hit the same pool, so `2 * 2` should not burn two slots on the
    // same number.
    Chunk chunk = compile_expr_source("2 * 2");
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(chunk.code[1] == chunk.code[3]);
}

TEST_CASE("a comparison compiles to both operands then the compare", "[compiler]") {
    // exactly the shape an addition has — the operator only works off what the
    // operands left behind, so nothing about the layout changes.
    Chunk chunk = compile_expr_source("1 < 2");
    REQUIRE(chunk.size() == 6);
    REQUIRE(op_at(chunk, 0) == Opcode::Const);
    REQUIRE(op_at(chunk, 2) == Opcode::Const);
    REQUIRE(op_at(chunk, 4) == Opcode::Less);
    REQUIRE(op_at(chunk, 5) == Opcode::Return);
}

TEST_CASE("the four comparisons with an opcode emit just that opcode", "[compiler]") {
    REQUIRE(op_at(compile_expr_source("1 == 2"), 4) == Opcode::Equal);
    REQUIRE(op_at(compile_expr_source("1 != 2"), 4) == Opcode::NotEqual);
    REQUIRE(op_at(compile_expr_source("1 < 2"), 4) == Opcode::Less);
    REQUIRE(op_at(compile_expr_source("1 > 2"), 4) == Opcode::Greater);
}

TEST_CASE("<= compiles to Greater followed by Not", "[compiler]") {
    // a <= b is not (a > b). the flipped opcode is the whole point, so checking
    // for Less here would be checking for the bug.
    Chunk chunk = compile_expr_source("1 <= 2");
    REQUIRE(chunk.size() == 7);
    REQUIRE(op_at(chunk, 4) == Opcode::Greater);
    REQUIRE(op_at(chunk, 5) == Opcode::Not);
    REQUIRE(op_at(chunk, 6) == Opcode::Return);
}

TEST_CASE(">= compiles to Less followed by Not", "[compiler]") {
    Chunk chunk = compile_expr_source("1 >= 2");
    REQUIRE(chunk.size() == 7);
    REQUIRE(op_at(chunk, 4) == Opcode::Less);
    REQUIRE(op_at(chunk, 5) == Opcode::Not);
}

TEST_CASE("a comparison keeps its operands in source order", "[compiler]") {
    // the same trap subtraction has, and worse here — the operands look
    // symmetric, so `3 < 8` compiled backwards still runs and just answers wrong.
    Chunk chunk = compile_expr_source("3 < 8");
    REQUIRE(std::get<int64_t>(chunk.constants[chunk.code[1]]) == 3);
    REQUIRE(std::get<int64_t>(chunk.constants[chunk.code[3]]) == 8);
}

TEST_CASE("the negated form does not flip the operands as well", "[compiler]") {
    // the inversion happens after the compare, not by swapping the sides. doing
    // both would cancel out and `3 <= 8` would answer as if it were `8 <= 3`.
    Chunk chunk = compile_expr_source("3 <= 8");
    REQUIRE(std::get<int64_t>(chunk.constants[chunk.code[1]]) == 3);
    REQUIRE(std::get<int64_t>(chunk.constants[chunk.code[3]]) == 8);
    REQUIRE(op_at(chunk, 4) == Opcode::Greater);
}

TEST_CASE("arithmetic binds tighter than comparison in the bytecode", "[compiler]") {
    // 1 + 2 < 4 groups as (1 + 2) < 4, so the Add has to be done before the Less
    // ever sees a value.
    Chunk chunk = compile_expr_source("1 + 2 < 4");
    REQUIRE(op_at(chunk, 4) == Opcode::Add);
    REQUIRE(op_at(chunk, 7) == Opcode::Less);
}

TEST_CASE("both instructions of a negated comparison get the operator's line", "[compiler]") {
    // the operands moved line_ to line 2 on the way past, and the Not is emitted
    // after the compare, so it is the one most likely to be left stamped wrong.
    Chunk chunk = compile_expr_source("1 <=\n2");
    REQUIRE(chunk.line_at(4) == 1);
    REQUIRE(chunk.line_at(5) == 1);
}

TEST_CASE("a unary minus is the operand followed by Negate", "[compiler]") {
    Chunk chunk = compile_expr_source("-7");
    REQUIRE(chunk.size() == 4);
    REQUIRE(op_at(chunk, 0) == Opcode::Const);
    REQUIRE(op_at(chunk, 2) == Opcode::Negate);
    REQUIRE(op_at(chunk, 3) == Opcode::Return);
}

TEST_CASE("the operand is compiled before the Negate, not folded into it", "[compiler]") {
    // 7 goes into the pool as a positive 7 and the sign is done at runtime. the
    // lexer never scanned a minus into the literal, so a -7 in the constants here
    // would mean something decided to fold on its own.
    Chunk chunk = compile_expr_source("-7");
    REQUIRE(std::get<int64_t>(chunk.constants[chunk.code[1]]) == 7);
}

TEST_CASE("a unary bang is the operand followed by Not", "[compiler]") {
    Chunk chunk = compile_expr_source("!true");
    REQUIRE(chunk.size() == 3);
    REQUIRE(op_at(chunk, 0) == Opcode::True);
    REQUIRE(op_at(chunk, 1) == Opcode::Not);
}

TEST_CASE("stacked unary operators nest instead of cancelling", "[compiler]") {
    // the parser builds one node inside another, so both come out. dropping the
    // pair is the peephole pass's job later, and doing it here would be folding
    // before there is anything that checks the fold is right.
    Chunk chunk = compile_expr_source("--7");
    REQUIRE(chunk.size() == 5);
    REQUIRE(op_at(chunk, 2) == Opcode::Negate);
    REQUIRE(op_at(chunk, 3) == Opcode::Negate);

    Chunk bangs = compile_expr_source("!!true");
    REQUIRE(op_at(bangs, 1) == Opcode::Not);
    REQUIRE(op_at(bangs, 2) == Opcode::Not);
}

TEST_CASE("unary binds tighter than a binary operator", "[compiler]") {
    // -2 + 3 is (-2) + 3, so the Negate has to land before the right operand is
    // pushed. if it came out after the Add it would negate the sum instead.
    Chunk chunk = compile_expr_source("-2 + 3");
    REQUIRE(op_at(chunk, 2) == Opcode::Negate);
    REQUIRE(op_at(chunk, 3) == Opcode::Const);
    REQUIRE(op_at(chunk, 5) == Opcode::Add);
}

TEST_CASE("a unary applies to a grouped expression as one operand", "[compiler]") {
    // -(2 + 3) has the whole Add underneath it, so the Negate is last and sees
    // the one value the group left behind.
    Chunk chunk = compile_expr_source("-(2 + 3)");
    REQUIRE(op_at(chunk, 4) == Opcode::Add);
    REQUIRE(op_at(chunk, 5) == Opcode::Negate);
}

TEST_CASE("the Negate takes the operator's line and not the operand's", "[compiler]") {
    // the operand pushed line_ on to line 2 on its way past, which is the same
    // trap the binary operators have.
    Chunk chunk = compile_expr_source("-\n7");
    REQUIRE(chunk.line_at(2) == 1);
}

TEST_CASE("a prefix operator with no meaning is reported", "[compiler]") {
    // the parser only builds a UnaryExpr for - and !, so this one has to be put
    // together by hand. it stands in for someone adding a prefix operator later
    // and forgetting the compiler side.
    Compiler compiler;
    UnaryExpr expr(Token(TokenKind::Star, "*", 1, 1),
                   std::make_unique<LiteralExpr>(Token(TokenKind::Integer, "1", 1, 2)));
    REQUIRE_THROWS_AS(compiler.compile_expression(expr), CompileError);
}

TEST_CASE("an operator with no opcode is reported, not skipped", "[compiler]") {
    // there is no token kind left that the parser builds a BinaryExpr for and
    // visit_binary has nothing for, so this one gets built by hand. it stands in
    // for someone adding an operator to the parser and forgetting this switch —
    // emitting the operands and no operator would leave two values on the stack
    // and the VM would carry on with the wrong one.
    Compiler compiler;
    BinaryExpr expr(std::make_unique<LiteralExpr>(Token(TokenKind::Integer, "1", 1, 1)),
                    Token(TokenKind::Comma, ",", 1, 3),
                    std::make_unique<LiteralExpr>(Token(TokenKind::Integer, "2", 1, 5)));
    REQUIRE_THROWS_AS(compiler.compile_expression(expr), CompileError);
}

TEST_CASE("a compile error says which line and column it came from", "[compiler]") {
    CompileError err(4, 9, "too many constants in one chunk");
    REQUIRE(err.line() == 4);
    REQUIRE(err.column() == 9);
    REQUIRE(std::string(err.what()) == "line 4:9: too many constants in one chunk");
}

TEST_CASE("a compile error with no column names only the line", "[compiler]") {
    // patch_jump is the one place with nothing to point at but the line the
    // chunk recorded. printing ":0" would look like a real column and send
    // whoever read it to the wrong place.
    CompileError err(4, 0, "too much code to jump over");
    REQUIRE(std::string(err.what()) == "line 4: too much code to jump over");
}

TEST_CASE("a let compiles its initializer and then binds the name", "[compiler]") {
    // the order is the whole instruction sequence: the value has to be on the
    // stack before DefineGlobal can take it off.
    Chunk chunk = compile_source("let x = 1");
    REQUIRE(op_at(chunk, 0) == Opcode::Const);
    REQUIRE(op_at(chunk, 2) == Opcode::DefineGlobal);
    REQUIRE(op_at(chunk, 4) == Opcode::Return);
}

TEST_CASE("the name a let binds goes in the constant pool as a string", "[compiler]") {
    Chunk chunk = compile_source("let count = 7");
    uint8_t index = chunk.code[3];
    REQUIRE(std::get<std::string>(chunk.constant_at(index)) == "count");
}

TEST_CASE("the value and the name are two separate pool entries", "[compiler]") {
    // easy to write this so the operand of one instruction points at the other's
    // constant, and the chunk still looks fine until it runs.
    Chunk chunk = compile_source("let x = 42");
    REQUIRE(chunk.constants.size() == 2);
    REQUIRE(std::get<int64_t>(chunk.constant_at(chunk.code[1])) == 42);
    REQUIRE(std::get<std::string>(chunk.constant_at(chunk.code[3])) == "x");
}

TEST_CASE("a let with a compound initializer still binds last", "[compiler]") {
    // the initializer is a whole subtree here, and DefineGlobal has to wait for
    // all of it rather than landing after the first operand.
    Chunk chunk = compile_source("let sum = 1 + 2 * 3");
    REQUIRE(op_at(chunk, chunk.size() - 3) == Opcode::DefineGlobal);
}

TEST_CASE("an identifier compiles to a GetGlobal naming it", "[compiler]") {
    Chunk chunk = compile_expr_source("total");
    REQUIRE(op_at(chunk, 0) == Opcode::GetGlobal);
    REQUIRE(std::get<std::string>(chunk.constant_at(chunk.code[1])) == "total");
}

TEST_CASE("a global read twice costs one pool slot", "[compiler]") {
    // add_constant already reuses an equal string, so nothing extra is needed
    // here — but if that ever stopped being true a program with a loop in it
    // would fill the pool on the variable names alone.
    Chunk chunk = compile_expr_source("n + n");
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(chunk.code[1] == chunk.code[3]);
}

TEST_CASE("a global sits where an operand goes in a bigger expression", "[compiler]") {
    // GetGlobal leaves exactly one value behind like every other expression, so
    // the Add above it doesn't have to know its operand came from a variable.
    Chunk chunk = compile_expr_source("x * 2");
    REQUIRE(op_at(chunk, 0) == Opcode::GetGlobal);
    REQUIRE(op_at(chunk, 2) == Opcode::Const);
    REQUIRE(op_at(chunk, 4) == Opcode::Mul);
}

TEST_CASE("the bind takes the let's line and not the initializer's", "[compiler]") {
    // same trap the binary operators have: the initializer walked line_ forward
    // on its way past, and an error on the bind should point at the `let`.
    Chunk chunk = compile_source("let x =\n1 +\n2");
    REQUIRE(chunk.line_at(chunk.size() - 3) == 1);
}

TEST_CASE("two lets bind under two different names", "[compiler]") {
    Chunk chunk = compile_source("let a = 1\nlet b = 2");
    REQUIRE(std::get<std::string>(chunk.constant_at(chunk.code[3])) == "a");
    REQUIRE(std::get<std::string>(chunk.constant_at(chunk.code[7])) == "b");
}

TEST_CASE("an expression statement compiles to the expression then a Pop", "[compiler]") {
    Chunk chunk = compile_source("1 + 2");

    // two Consts with an index each, the Add, the Pop, and the Return on the end.
    REQUIRE(chunk.size() == 7);
    REQUIRE(op_at(chunk, 4) == Opcode::Add);
    REQUIRE(op_at(chunk, 5) == Opcode::Pop);
    REQUIRE(op_at(chunk, 6) == Opcode::Return);
}

TEST_CASE("the Pop throws away the result and not the work", "[compiler]") {
    // both reads and the Add are still emitted. dropping the value at the end is
    // not the same as deciding the expression doesn't need compiling — it can
    // have side effects, and once calls land in step 82 that is the only reason
    // most expression statements are written at all.
    Chunk chunk = compile_source("let a = 1\na + a");
    REQUIRE(op_at(chunk, 4) == Opcode::GetGlobal);
    REQUIRE(op_at(chunk, 6) == Opcode::GetGlobal);
    REQUIRE(op_at(chunk, 8) == Opcode::Add);
    REQUIRE(op_at(chunk, 9) == Opcode::Pop);
}

TEST_CASE("each expression statement gets a Pop of its own", "[compiler]") {
    Chunk chunk = compile_source("1\n2");

    // Const, index, Pop for the first, the same three for the second, Return.
    REQUIRE(chunk.size() == 7);
    REQUIRE(op_at(chunk, 2) == Opcode::Pop);
    REQUIRE(op_at(chunk, 5) == Opcode::Pop);
}

TEST_CASE("the Pop is stamped with the line its expression ended on", "[compiler]") {
    // there is no token of its own to take a line from, so it keeps whatever the
    // expression left behind. that is the right answer anyway — the Pop belongs
    // to the end of the statement.
    Chunk chunk = compile_source("1\n2");
    REQUIRE(chunk.line_at(2) == 1);
    REQUIRE(chunk.line_at(5) == 2);
}

TEST_CASE("a program of expression statements ends with an empty stack", "[compiler]") {
    VM vm;
    run_source(vm, "1 2 3 4 5 6 7 8 9 10");
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("mixing lets and expression statements still balances out", "[compiler]") {
    // the invariant the rest of the phase is built on: an expression leaves one
    // value behind, a statement leaves none. a while loop jumps back to its
    // condition on every turn, so a body that gained a value per pass would grow
    // the stack with the iteration count.
    VM vm;
    run_source(vm, "let a = 1\na + 2\nlet b = a + 3\nb < 10");
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("the statement still leaves its value bound where it belongs", "[compiler]") {
    // balancing the stack is not the same as throwing the program away. the
    // globals are untouched by the Pop.
    VM vm;
    run_source(vm, "let a = 4\na * 2");
    REQUIRE(vm.global("a") != nullptr);
    REQUIRE(std::get<int64_t>(*vm.global("a")) == 4);
}

TEST_CASE("a let inside a block emits no bind instruction at all", "[compiler]") {
    // the initializer already put the value on the stack and that spot is the
    // variable, so there is nothing left to do. Const, its index, the Pop that
    // end_scope emits, and the Return.
    Chunk chunk = compile_source("{ let x = 1 }");
    REQUIRE(chunk.size() == 4);
    REQUIRE(op_at(chunk, 0) == Opcode::Const);
    REQUIRE(op_at(chunk, 2) == Opcode::Pop);
    REQUIRE(op_at(chunk, 3) == Opcode::Return);
}

TEST_CASE("a local's name never reaches the constant pool", "[compiler]") {
    // that is the saving over a global: no pool entry and no hash lookup, just a
    // slot number the compiler worked out while it was walking.
    Chunk chunk = compile_source("{ let counter = 1 }");
    REQUIRE(chunk.constants.size() == 1);
    REQUIRE(std::get<int64_t>(chunk.constant_at(0)) == 1);
}

TEST_CASE("reading a local compiles to GetLocal and its slot", "[compiler]") {
    Chunk chunk = compile_source("{ let x = 7 x }");
    REQUIRE(op_at(chunk, 2) == Opcode::GetLocal);
    REQUIRE(chunk.code[3] == 0);
}

TEST_CASE("locals are numbered in declaration order", "[compiler]") {
    Chunk chunk = compile_source("{ let a = 1 let b = 2 b a }");

    // Const/index twice, then the two reads. b was declared second so it is in
    // slot 1, whichever order they are read back in.
    REQUIRE(op_at(chunk, 4) == Opcode::GetLocal);
    REQUIRE(chunk.code[5] == 1);
    REQUIRE(op_at(chunk, 7) == Opcode::GetLocal);
    REQUIRE(chunk.code[8] == 0);
}

TEST_CASE("a name that is not a local still compiles to a global", "[compiler]") {
    Chunk chunk = compile_source("let outer = 1\n{ outer }");
    REQUIRE(op_at(chunk, 4) == Opcode::GetGlobal);
}

TEST_CASE("a local shadows a global of the same name", "[compiler]") {
    // resolve_local runs first, so inside the block the name means the slot and
    // not the map entry. the other way round and the block would read whatever
    // the file bound at the top.
    Chunk chunk = compile_source("let x = 1\n{ let x = 2 x }");
    REQUIRE(op_at(chunk, 6) == Opcode::GetLocal);
}

TEST_CASE("an inner local shadows an outer one", "[compiler]") {
    // searching locals_ backwards is what does this. forwards would find the
    // outer x, which is still in scope and still has a slot.
    Chunk chunk = compile_source("{ let x = 1 { let x = 2 x } }");
    REQUIRE(op_at(chunk, 4) == Opcode::GetLocal);
    REQUIRE(chunk.code[5] == 1);
}

TEST_CASE("the outer local is back once the inner block ends", "[compiler]") {
    Chunk chunk = compile_source("{ let x = 1 { let x = 2 } x }");

    // the inner block's Pop, then the read, which is slot 0 again.
    REQUIRE(op_at(chunk, 4) == Opcode::Pop);
    REQUIRE(op_at(chunk, 5) == Opcode::GetLocal);
    REQUIRE(chunk.code[6] == 0);
}

TEST_CASE("an initializer reads the outer variable and not the one being declared",
          "[compiler]") {
    // the local is only added after its initializer compiled, so the x on the
    // right is the outer one. Interpreter::visit_let gets the same answer for
    // the same reason — it calls define() once it has a value.
    Chunk chunk = compile_source("{ let x = 1 { let x = x } }");
    REQUIRE(op_at(chunk, 2) == Opcode::GetLocal);
    REQUIRE(chunk.code[3] == 0);
}

TEST_CASE("every local in a block is popped on the way out", "[compiler]") {
    Chunk chunk = compile_source("{ let a = 1 let b = 2 let c = 3 }");

    // three Const/index pairs and then one Pop each.
    REQUIRE(op_at(chunk, 6) == Opcode::Pop);
    REQUIRE(op_at(chunk, 7) == Opcode::Pop);
    REQUIRE(op_at(chunk, 8) == Opcode::Pop);
    REQUIRE(op_at(chunk, 9) == Opcode::Return);
}

TEST_CASE("a block with no locals in it pops nothing", "[compiler]") {
    // the Pops belong to the declarations, not to the braces.
    Chunk chunk = compile_source("{ 1 }");
    REQUIRE(chunk.size() == 4);
    REQUIRE(op_at(chunk, 2) == Opcode::Pop);
    REQUIRE(op_at(chunk, 3) == Opcode::Return);
}

TEST_CASE("a nested block pops its own locals and the outer ones separately", "[compiler]") {
    Chunk chunk = compile_source("{ let a = 1 { let b = 2 let c = 3 } }");

    // three declarations, three Pops, and end_scope ran twice to emit them —
    // twice for b and c when the inner block closed, once more for a at the end.
    // which Pop belongs to which block is what the shadowing test above pins
    // down; all this counts is that none of them went missing.
    REQUIRE(op_at(chunk, 6) == Opcode::Pop);
    REQUIRE(op_at(chunk, 7) == Opcode::Pop);
    REQUIRE(op_at(chunk, 8) == Opcode::Pop);
    REQUIRE(op_at(chunk, 9) == Opcode::Return);
}

TEST_CASE("a let at the top level is still a global", "[compiler]") {
    // depth 0 has no stack slot to live in that would outlast the statement, and
    // a function compiled further down the file has to be able to name it.
    Chunk chunk = compile_source("let x = 1");
    REQUIRE(op_at(chunk, 2) == Opcode::DefineGlobal);
}

TEST_CASE("more locals than a slot number can name is a compile error", "[compiler]") {
    std::string source = "{\n";
    for (int i = 0; i < 257; ++i) {
        source += "let v" + std::to_string(i) + " = 1\n";
    }
    source += "}";

    std::vector<std::unique_ptr<Stmt>> program = parse_program(source);
    Compiler compiler;
    REQUIRE_THROWS_AS(compiler.compile(program), CompileError);
}

TEST_CASE("256 locals is still fine", "[compiler]") {
    // the limit is what one byte can name, so the 256th is the last one that
    // fits and the error belongs on the one after it.
    std::string source = "{\n";
    for (int i = 0; i < 256; ++i) {
        source += "let v" + std::to_string(i) + " = 1\n";
    }
    source += "}";

    std::vector<std::unique_ptr<Stmt>> program = parse_program(source);
    Compiler compiler;
    REQUIRE_NOTHROW(compiler.compile(program));
}

TEST_CASE("a block leaves the stack the way it found it", "[compiler]") {
    VM vm;
    run_source(vm, "{ let a = 1 let b = a + 1 b }");
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("a local reads back the value that was bound to it", "[compiler]") {
    // there is no way to see a local from outside the block, since the Pop takes
    // it away again, so this goes at it through an error the value decides: the
    // message names the type of whatever the GetLocal pushed.
    Chunk chunk = compile_source("{ let x = \"hi\" x + 1 }");

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(std::string(vm.error()->what()) == "cannot apply '+' to string and int");
}

TEST_CASE("the block's local does not touch the global it shadows", "[compiler]") {
    // the whole point of the two being different storage. the block binds its own
    // x in a slot and the global keeps the value it had.
    VM vm;
    run_source(vm, "let x = 1\n{ let x = 99 }");
    REQUIRE(std::get<int64_t>(*vm.global("x")) == 1);
}

TEST_CASE("a compiler reused after a block starts at the top level again", "[compiler]") {
    // scope_depth_ and locals_ both survive the compile unless reset clears them,
    // and a second program starting at depth 1 would turn its top-level lets into
    // locals nobody ever pops.
    std::vector<std::unique_ptr<Stmt>> first = parse_program("{ let a = 1 }");
    std::vector<std::unique_ptr<Stmt>> second = parse_program("let b = 2");

    Compiler compiler;
    compiler.compile(first);
    Chunk chunk = compiler.compile(second);

    REQUIRE(op_at(chunk, 2) == Opcode::DefineGlobal);
}

TEST_CASE("an assignment compiles to the value then a store", "[compiler]") {
    // Const 2, SetGlobal "a", and the Pop that belongs to the statement around
    // it. the store comes last for the same reason an Add does — it works off
    // what the value already left on the stack.
    Chunk chunk = compile_source("let a = 1\na = 2");
    REQUIRE(op_at(chunk, 4) == Opcode::Const);
    REQUIRE(op_at(chunk, 6) == Opcode::SetGlobal);
    REQUIRE(op_at(chunk, 8) == Opcode::Pop);
}

TEST_CASE("the store names the same pool entry the let bound", "[compiler]") {
    // both instructions carry an index into the pool, and add_constant dedupes
    // strings, so writing the name twice has to come back as the same byte. if it
    // didn't the assignment would be reaching for a global nobody defined.
    Chunk chunk = compile_source("let a = 1\na = 2");
    REQUIRE(chunk.code[3] == chunk.code[7]);
    REQUIRE(std::get<std::string>(chunk.constant_at(chunk.code[7])) == "a");
}

TEST_CASE("only one Pop comes out of an assignment statement", "[compiler]") {
    // visit_assign emits no Pop of its own — the value is what the expression
    // evaluates to. a second one here would eat whatever was under it.
    Chunk chunk = compile_source("let a = 1\na = 2");
    REQUIRE(chunk.size() == 10);
    REQUIRE(op_at(chunk, 9) == Opcode::Return);
}

TEST_CASE("assigning to a local compiles to SetLocal and its slot", "[compiler]") {
    Chunk chunk = compile_source("{ let x = 1 x = 2 }");
    REQUIRE(op_at(chunk, 4) == Opcode::SetLocal);
    REQUIRE(chunk.code[5] == 0);
}

TEST_CASE("a local target keeps its name out of the pool", "[compiler]") {
    // same saving as reading one. only the two literals should be in there.
    Chunk chunk = compile_source("{ let x = 1 x = 2 }");
    REQUIRE(chunk.constants.size() == 2);
}

TEST_CASE("the second local gets the slot it was declared in", "[compiler]") {
    // neither let emits a bind instruction, so the two Const pairs are all that
    // comes before the value being assigned.
    Chunk chunk = compile_source("{ let a = 1 let b = 2 b = 3 }");
    REQUIRE(op_at(chunk, 4) == Opcode::Const);
    REQUIRE(op_at(chunk, 6) == Opcode::SetLocal);
    REQUIRE(chunk.code[7] == 1);
}

TEST_CASE("a local shadows a global on the left of an assignment too", "[compiler]") {
    // resolve_local runs first here just like it does in visit_identifier, so
    // the write goes to the slot. the other way round and a block would clobber
    // the file's variable every time it assigned to its own.
    Chunk chunk = compile_source("let x = 1\n{ let x = 2 x = 3 }");
    REQUIRE(op_at(chunk, 8) == Opcode::SetLocal);
}

TEST_CASE("a name with no local of that name falls through to SetGlobal", "[compiler]") {
    Chunk chunk = compile_source("let outer = 1\n{ outer = 2 }");
    REQUIRE(op_at(chunk, 6) == Opcode::SetGlobal);
}

TEST_CASE("a chained assignment stores into both names", "[compiler]") {
    // `a = b = 7` parses right to left, so the inner assignment is the outer
    // one's value. it stores into b and leaves the 7 behind, which is then what
    // the outer store writes into a.
    Chunk chunk = compile_source("let a = 1\nlet b = 2\na = b = 7");
    REQUIRE(op_at(chunk, 8) == Opcode::Const);
    REQUIRE(op_at(chunk, 10) == Opcode::SetGlobal);
    REQUIRE(std::get<std::string>(chunk.constant_at(chunk.code[11])) == "b");
    REQUIRE(op_at(chunk, 12) == Opcode::SetGlobal);
    REQUIRE(std::get<std::string>(chunk.constant_at(chunk.code[13])) == "a");
}

TEST_CASE("the store is stamped with the name's line and not the value's", "[compiler]") {
    // same trap every other visitor has: the right-hand side moves line_ along
    // as it compiles, so an assignment split over two lines would blame the
    // wrong one if set_line didn't run before the emit.
    Chunk chunk = compile_source("let a = 1\na =\n2");
    REQUIRE(chunk.line_at(6) == 2);
}

TEST_CASE("an assignment writes over the global it names", "[compiler]") {
    VM vm;
    run_source(vm, "let a = 1\na = 2");
    REQUIRE(std::get<int64_t>(*vm.global("a")) == 2);
}

TEST_CASE("the assigned value is still there for the expression around it", "[compiler]") {
    // this is what makes the chain work. if visit_assign popped, b would be
    // bound to whatever was underneath instead of to the 7.
    VM vm;
    run_source(vm, "let a = 1\nlet b = a = 7");
    REQUIRE(std::get<int64_t>(*vm.global("a")) == 7);
    REQUIRE(std::get<int64_t>(*vm.global("b")) == 7);
}

TEST_CASE("an assignment statement still balances the stack", "[compiler]") {
    VM vm;
    run_source(vm, "let a = 1\na = 2\na = a + 1");
    REQUIRE(vm.stack_size() == 0);
    REQUIRE(std::get<int64_t>(*vm.global("a")) == 3);
}

TEST_CASE("assigning a name nobody bound is a runtime error", "[compiler]") {
    // not a definition. a typo on the left of an `=` should be caught, not turned
    // into a second variable, and that is the rule Environment::assign follows in
    // the tree-walker.
    Chunk chunk = compile_source("nope = 1");

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(vm.error() != nullptr);
    REQUIRE(std::string(vm.error()->what()) == "undefined variable 'nope'");
}

TEST_CASE("assigning to a local leaves the global it shadows alone", "[compiler]") {
    VM vm;
    run_source(vm, "let x = 1\n{ let x = 2 x = 99 }");
    REQUIRE(std::get<int64_t>(*vm.global("x")) == 1);
}

TEST_CASE("a local keeps the value assigned to it for the rest of the block", "[compiler]") {
    // the slot is written in place, so the read after it finds the new value.
    // going at it through an error again, since the local is gone by the time the
    // block ends and there is nothing left to inspect from outside.
    Chunk chunk = compile_source("{ let x = 1 x = \"hi\" x + 1 }");

    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::RuntimeError);
    REQUIRE(std::string(vm.error()->what()) == "cannot apply '+' to string and int");
}

TEST_CASE("an if with no else jumps over the then branch", "[compiler]") {
    // Const 1, Const 2, Less, then the jump. the branch is a block holding one
    // `let`, which at depth 1 is a local: the Const that runs its initializer is
    // the whole of it, and the Pop is end_scope taking the slot back.
    Chunk chunk = compile_source("if 1 < 2 { let y = 3 }");
    REQUIRE(op_at(chunk, 4) == Opcode::Less);
    REQUIRE(op_at(chunk, 5) == Opcode::JumpIfFalse);
    REQUIRE(op_at(chunk, 8) == Opcode::Const);
    REQUIRE(op_at(chunk, 10) == Opcode::Pop);
    REQUIRE(op_at(chunk, 11) == Opcode::Return);
}

TEST_CASE("the patched distance lands past the end of the then branch", "[compiler]") {
    // measured from the instruction after the jump, so 8 + 3 is the Return.
    Chunk chunk = compile_source("if 1 < 2 { let y = 3 }");
    std::size_t distance = (static_cast<std::size_t>(chunk.code[6]) << 8) | chunk.code[7];
    REQUIRE(8 + distance == 11);
}

TEST_CASE("nothing pops the condition, the jump does it", "[compiler]") {
    // the condition is the only thing an if pushes and JumpIfFalse is the only
    // thing that takes it off. an extra Pop here would eat whatever was under it
    // on the branch that skips.
    Chunk chunk = compile_source("if 1 < 2 { let y = 3 }");
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        if (op_at(chunk, i) == Opcode::JumpIfFalse) {
            REQUIRE(op_at(chunk, i + 3) != Opcode::Pop);
        }
    }
}

TEST_CASE("an if with an else steps over it on the way out", "[compiler]") {
    // the false side lands on the first instruction of the else branch, and the
    // true side has to jump past that branch or it would run both halves.
    Chunk chunk = compile_source("if 1 { 2 } else { 3 }");
    REQUIRE(op_at(chunk, 2) == Opcode::JumpIfFalse);
    REQUIRE(op_at(chunk, 8) == Opcode::Jump);
    REQUIRE(op_at(chunk, 11) == Opcode::Const);
    REQUIRE(op_at(chunk, 14) == Opcode::Return);

    std::size_t to_else = (static_cast<std::size_t>(chunk.code[3]) << 8) | chunk.code[4];
    REQUIRE(5 + to_else == 11);

    std::size_t past_else = (static_cast<std::size_t>(chunk.code[9]) << 8) | chunk.code[10];
    REQUIRE(11 + past_else == 14);
}

TEST_CASE("an else if is just another if hanging off the else", "[compiler]") {
    // the parser nests the second one, so compiling the else branch walks
    // straight back into visit_if and there is a JumpIfFalse per condition.
    Chunk chunk = compile_source("if 1 { 2 } else if 3 { 4 } else { 5 }");

    int conditions = 0;
    std::size_t offset = 0;
    while (offset < chunk.size()) {
        if (op_at(chunk, offset) == Opcode::JumpIfFalse) {
            ++conditions;
        }
        offset += instruction_size(chunk, offset);
    }
    REQUIRE(conditions == 2);
}

TEST_CASE("a branch is its own scope", "[compiler]") {
    // the `let` inside the braces is a local and not a global, same as any other
    // block, so nothing goes in the pool and the slot is dropped on the way out.
    Chunk chunk = compile_source("if 1 { let y = 2 }");
    REQUIRE(chunk.constants.size() == 2);
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        REQUIRE(op_at(chunk, i) != Opcode::DefineGlobal);
    }
}

TEST_CASE("only the branch the condition picked runs", "[compiler]") {
    VM vm;
    run_source(vm, "let taken = 0\nif 1 < 2 { taken = 1 } else { taken = 2 }");
    REQUIRE(std::get<int64_t>(*vm.global("taken")) == 1);
}

TEST_CASE("a false condition runs the else branch instead", "[compiler]") {
    VM vm;
    run_source(vm, "let taken = 0\nif 2 < 1 { taken = 1 } else { taken = 2 }");
    REQUIRE(std::get<int64_t>(*vm.global("taken")) == 2);
}

TEST_CASE("an if with no else and a false condition does nothing", "[compiler]") {
    VM vm;
    run_source(vm, "let taken = 0\nif false { taken = 1 }");
    REQUIRE(std::get<int64_t>(*vm.global("taken")) == 0);
}

TEST_CASE("an else if chain stops at the first condition that holds", "[compiler]") {
    VM vm;
    run_source(vm, "let n = 0\nif false { n = 1 } else if true { n = 2 } else { n = 3 }");
    REQUIRE(std::get<int64_t>(*vm.global("n")) == 2);
}

TEST_CASE("the branch runs on the same truthiness the tree-walker uses", "[compiler]") {
    // only nil and false are falsy, so `if 0` runs its branch. worth pinning
    // because it is the kind of rule the two backends could quietly disagree on.
    VM vm;
    run_source(vm, "let n = 0\nif 0 { n = 1 }\nif nil { n = n + 10 }");
    REQUIRE(std::get<int64_t>(*vm.global("n")) == 1);
}

TEST_CASE("an if statement leaves the stack the way it found it", "[compiler]") {
    // the condition goes on and JumpIfFalse takes it off, whichever way the
    // branch went. a while loop in the next step runs this over and over, so one
    // value left behind per pass would grow the stack with the iteration count.
    VM vm;
    run_source(vm, "let n = 1\nif n { n = 2 }\nif false { n = 3 } else { n = 4 }");
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("ifs nest without the inner jumps disturbing the outer ones", "[compiler]") {
    VM vm;
    run_source(vm, "let n = 0\nif true { if false { n = 1 } else { n = 2 } }");
    REQUIRE(std::get<int64_t>(*vm.global("n")) == 2);
}

TEST_CASE("the disassembler shows where a compiled jump lands", "[compiler]") {
    // the whole reason the operand gets printed with its target: reading the
    // distance out of a dump and adding it by hand is exactly the arithmetic
    // that goes wrong.
    Chunk chunk = compile_source("if 1 { 2 }");
    std::string out = disassemble(chunk, "if");
    REQUIRE(out.find("JumpIfFalse") != std::string::npos);
    REQUIRE(out.find("-> 8") != std::string::npos);
}

TEST_CASE("a branch too long to jump over is a compile error", "[compiler]") {
    // the operand is two bytes, so a then branch past 65535 bytes of code cannot
    // be described. writing the low half anyway would land the jump somewhere in
    // the middle of the branch.
    std::string source = "if 1 {\n";
    for (int i = 0; i < 25000; ++i) {
        // three bytes each: Const, its pool index, and the Pop from the
        // expression statement. the 1 is only in the pool once.
        source += "1\n";
    }
    source += "}";

    REQUIRE_THROWS_AS(compile_source(source), CompileError);
}

TEST_CASE("&& jumps over the right operand and pushes false instead", "[compiler]") {
    // False, the jump, then the right side and the two Nots that make it a bool,
    // a Jump past the short-circuit answer, and that answer.
    Chunk chunk = compile_expr_source("false && true");
    REQUIRE(op_at(chunk, 0) == Opcode::False);
    REQUIRE(op_at(chunk, 1) == Opcode::JumpIfFalse);
    REQUIRE(op_at(chunk, 4) == Opcode::True);
    REQUIRE(op_at(chunk, 5) == Opcode::Not);
    REQUIRE(op_at(chunk, 6) == Opcode::Not);
    REQUIRE(op_at(chunk, 7) == Opcode::Jump);
    REQUIRE(op_at(chunk, 10) == Opcode::False);
    REQUIRE(op_at(chunk, 11) == Opcode::Return);

    // the falsy path lands on that False and skips everything in between.
    std::size_t to_false = (static_cast<std::size_t>(chunk.code[2]) << 8) | chunk.code[3];
    REQUIRE(4 + to_false == 10);
}

TEST_CASE("|| is the same shape with the two sides swapped", "[compiler]") {
    // a true left operand settles ||, so this time it's the fall-through that
    // pushes the answer and the jump that leads to the right operand.
    Chunk chunk = compile_expr_source("false || true");
    REQUIRE(op_at(chunk, 1) == Opcode::JumpIfFalse);
    REQUIRE(op_at(chunk, 4) == Opcode::True);
    REQUIRE(op_at(chunk, 5) == Opcode::Jump);
    REQUIRE(op_at(chunk, 8) == Opcode::True);
    REQUIRE(op_at(chunk, 9) == Opcode::Not);
    REQUIRE(op_at(chunk, 11) == Opcode::Return);

    std::size_t to_right = (static_cast<std::size_t>(chunk.code[2]) << 8) | chunk.code[3];
    REQUIRE(4 + to_right == 8);
}

TEST_CASE("neither operator emits an instruction of its own", "[compiler]") {
    // there is no And or Or opcode and there shouldn't be one — if this starts
    // failing it means someone added a plain instruction and lost the
    // short-circuit with it.
    for (const std::string& source : {std::string("true && false"), std::string("true || false")}) {
        Chunk chunk = compile_expr_source(source);
        std::size_t jumps = 0;
        std::size_t offset = 0;
        while (offset < chunk.size()) {
            Opcode op = op_at(chunk, offset);
            if (op == Opcode::Jump || op == Opcode::JumpIfFalse) {
                ++jumps;
            }
            offset += instruction_size(chunk, offset);
        }
        REQUIRE(jumps == 2);
    }
}

TEST_CASE("the jumps take the operator's line and not the operands'", "[compiler]") {
    Chunk chunk = compile_expr_source("true\n&&\nfalse");
    REQUIRE(chunk.line_at(1) == 2);
}

TEST_CASE("a false left operand keeps the right one from running at all", "[compiler]") {
    // the test that actually proves the jump was taken: the divide would stop the
    // VM if anything reached it.
    VM vm;
    run_source(vm, "let r = false && 1 / 0");
    REQUIRE(std::get<bool>(*vm.global("r")) == false);
}

TEST_CASE("a true left operand does the same for ||", "[compiler]") {
    VM vm;
    run_source(vm, "let r = true || 1 / 0");
    REQUIRE(std::get<bool>(*vm.global("r")) == true);
}

TEST_CASE("the skipped side isn't even looked up", "[compiler]") {
    // `nope` is bound nowhere, so a GetGlobal reaching it would be an undefined
    // variable error. same point as the divide, from the other direction.
    VM vm;
    run_source(vm, "let r = false && nope");
    REQUIRE(std::get<bool>(*vm.global("r")) == false);
}

TEST_CASE("the right operand decides it when the left one doesn't", "[compiler]") {
    VM vm;
    run_source(vm, "let a = true && false\nlet b = false || true\nlet c = true && true");
    REQUIRE(std::get<bool>(*vm.global("a")) == false);
    REQUIRE(std::get<bool>(*vm.global("b")) == true);
    REQUIRE(std::get<bool>(*vm.global("c")) == true);
}

TEST_CASE("the answer comes back as a bool and not as the operand", "[compiler]") {
    // `1 && 2` is true, not 2, which is what the tree-walker says. the two Nots
    // are the whole reason this holds.
    VM vm;
    run_source(vm, "let r = 1 && 2");
    REQUIRE(std::holds_alternative<bool>(*vm.global("r")));
    REQUIRE(std::get<bool>(*vm.global("r")) == true);
}

TEST_CASE("both operators use the same truthiness as everything else", "[compiler]") {
    // only nil and false are falsy, so 0 counts as true here.
    VM vm;
    run_source(vm, "let a = 0 && true\nlet b = nil || false");
    REQUIRE(std::get<bool>(*vm.global("a")) == true);
    REQUIRE(std::get<bool>(*vm.global("b")) == false);
}

TEST_CASE("a logical expression leaves one value on the stack", "[compiler]") {
    // whichever path it took. one branch pushes the operand's bool and the other
    // pushes a literal, so a mismatch here would show up as drift over a loop.
    Chunk chunk = compile_expr_source("false && true");
    VM vm;
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
    REQUIRE(vm.stack_size() == 1);
}

TEST_CASE("&& binds tighter than || the way the parser nested them", "[compiler]") {
    // `false && false` is the right operand of the ||, so the || answers with it
    // and the whole thing is false. getting the nesting backwards gives true.
    VM vm;
    run_source(vm, "let r = false || false && false");
    REQUIRE(std::get<bool>(*vm.global("r")) == false);
}

TEST_CASE("comparisons sit inside them without any bracketing", "[compiler]") {
    // the case the short-circuit is really for: the second half is only safe
    // because the first one guards it.
    VM vm;
    run_source(vm, "let n = 0\nlet r = n != 0 && 10 / n > 1");
    REQUIRE(std::get<bool>(*vm.global("r")) == false);
}

TEST_CASE("they nest inside each other without the jumps crossing", "[compiler]") {
    VM vm;
    run_source(vm, "let r = (true && false) || (true && true)");
    REQUIRE(std::get<bool>(*vm.global("r")) == true);
}

TEST_CASE("a logical operator works as an if condition", "[compiler]") {
    VM vm;
    run_source(vm, "let n = 0\nif 1 < 2 && 3 > 2 { n = 1 }");
    REQUIRE(std::get<int64_t>(*vm.global("n")) == 1);
}

TEST_CASE("a while loop ends with a Loop back to its condition", "[compiler]") {
    // False, the exit jump, the body's Const and Pop, then the jump back. the
    // condition is inside the loop, so 0 is what the Loop has to land on — a
    // target after it would test once and run forever.
    Chunk chunk = compile_source("while false { 1 }");
    REQUIRE(op_at(chunk, 0) == Opcode::False);
    REQUIRE(op_at(chunk, 1) == Opcode::JumpIfFalse);
    REQUIRE(op_at(chunk, 4) == Opcode::Const);
    REQUIRE(op_at(chunk, 6) == Opcode::Pop);
    REQUIRE(op_at(chunk, 7) == Opcode::Loop);
    REQUIRE(op_at(chunk, 10) == Opcode::Return);

    // counted back from the end of the instruction, so 10 - 10 is offset 0.
    std::size_t distance = (static_cast<std::size_t>(chunk.code[8]) << 8) | chunk.code[9];
    REQUIRE(10 - distance == 0);
}

TEST_CASE("the exit jump lands past the Loop and not on it", "[compiler]") {
    // patching after the Loop is written is what puts it there. landing on the
    // Loop would send a finished condition straight back into the body.
    Chunk chunk = compile_source("while false { 1 }");
    std::size_t distance = (static_cast<std::size_t>(chunk.code[2]) << 8) | chunk.code[3];
    REQUIRE(4 + distance == 10);
    REQUIRE(op_at(chunk, 4 + distance) == Opcode::Return);
}

TEST_CASE("the loop condition is not popped by anything but the jump", "[compiler]") {
    // plan.md asks for a Pop after the exit jump and another after the patch.
    // both were written expecting JumpIfFalse to leave the condition behind, and
    // it doesn't — either one would eat a value per pass off whatever the loop
    // was sitting on top of.
    Chunk chunk = compile_source("while false { 1 }");
    REQUIRE(op_at(chunk, 4) != Opcode::Pop);
    REQUIRE(op_at(chunk, 10) != Opcode::Pop);
}

TEST_CASE("a loop body is its own scope like any other block", "[compiler]") {
    // the `let` inside is a local, so nothing goes in the pool for it and the
    // slot is given back before the Loop rather than after it.
    Chunk chunk = compile_source("while 1 { let y = 2 }");
    REQUIRE(op_at(chunk, 7) == Opcode::Pop);
    REQUIRE(op_at(chunk, 8) == Opcode::Loop);
    for (std::size_t i = 0; i < chunk.size(); ++i) {
        REQUIRE(op_at(chunk, i) != Opcode::DefineGlobal);
    }
}

TEST_CASE("a false condition runs the body no times at all", "[compiler]") {
    VM vm;
    run_source(vm, "let n = 0\nwhile false { n = 1 }");
    REQUIRE(std::get<int64_t>(*vm.global("n")) == 0);
}

TEST_CASE("a counting loop stops after the right number of passes", "[compiler]") {
    // the condition being re-read every time round is the whole thing this is
    // checking. a Loop landing after it would never see n reach 5.
    VM vm;
    run_source(vm, "let n = 0\nlet total = 0\nwhile n < 5 { n = n + 1 total = total + n }");
    REQUIRE(std::get<int64_t>(*vm.global("n")) == 5);
    REQUIRE(std::get<int64_t>(*vm.global("total")) == 15);
}

TEST_CASE("the stack is where it started once the loop is done", "[compiler]") {
    // the one that would catch a stray Pop or a missing one. an if gets away with
    // being one out, a loop multiplies it by the iteration count.
    VM vm;
    run_source(vm, "let n = 0\nwhile n < 20 { n = n + 1 }");
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("a local declared in the body is dropped on every pass", "[compiler]") {
    // twenty turns with the slot left behind each time is twenty values piled up
    // under the condition, so the stack check is the real assertion here.
    VM vm;
    run_source(vm, "let n = 0\nlet last = 0\n"
                   "while n < 3 { let doubled = n * 2 last = doubled n = n + 1 }");
    REQUIRE(std::get<int64_t>(*vm.global("last")) == 4);
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("loops nest without the inner jumps disturbing the outer ones", "[compiler]") {
    VM vm;
    run_source(vm,
               "let rows = 0\nlet cells = 0\n"
               "while rows < 3 { let cols = 0 while cols < 4 { cols = cols + 1 cells = cells + 1 }"
               " rows = rows + 1 }");
    REQUIRE(std::get<int64_t>(*vm.global("cells")) == 12);
    REQUIRE(vm.stack_size() == 0);
}

TEST_CASE("an if inside a loop keeps its own jumps straight", "[compiler]") {
    VM vm;
    run_source(vm, "let n = 0\nlet evens = 0\n"
                   "while n < 6 { if n % 2 == 0 { evens = evens + 1 } n = n + 1 }");
    REQUIRE(std::get<int64_t>(*vm.global("evens")) == 3);
}

TEST_CASE("the condition uses the same truthiness as everything else", "[compiler]") {
    // 0 is truthy, so the body runs once and the assignment inside it is what
    // ends the loop. nil is falsy and the second one never starts.
    VM vm;
    run_source(vm, "let flag = 0\nlet runs = 0\nwhile flag { flag = false runs = runs + 1 }\n"
                   "let skipped = 0\nwhile nil { skipped = 1 }");
    REQUIRE(std::get<int64_t>(*vm.global("runs")) == 1);
    REQUIRE(std::get<int64_t>(*vm.global("skipped")) == 0);
}

TEST_CASE("a short-circuiting condition works as a loop guard", "[compiler]") {
    // the divide is only reached while n is not 0, which is the case && exists
    // for and the loop re-checks it every pass. it runs 4 down to 1 — 8 / 1 is
    // still greater than 1 — and the guard is what stops it at 0 instead of
    // dividing by it.
    VM vm;
    run_source(vm,
               "let n = 4\nlet steps = 0\n"
               "while n != 0 && 8 / n > 1 { n = n - 1 steps = steps + 1 }");
    REQUIRE(std::get<int64_t>(*vm.global("steps")) == 4);
    REQUIRE(std::get<int64_t>(*vm.global("n")) == 0);
}

TEST_CASE("the Loop takes the loop's line and not the end of the body", "[compiler]") {
    // the body can run for pages, and a back-jump blaming its last line would
    // point nowhere near the loop it belongs to.
    Chunk chunk = compile_source("let n = 3\nwhile n > 0 {\nn = n - 1\n}");
    std::size_t offset = 0;
    while (offset < chunk.size() && op_at(chunk, offset) != Opcode::Loop) {
        offset += instruction_size(chunk, offset);
    }
    REQUIRE(offset < chunk.size());
    REQUIRE(chunk.line_at(offset) == 2);
}

TEST_CASE("the disassembler counts a compiled Loop backwards", "[compiler]") {
    Chunk chunk = compile_source("while false { 1 }");
    std::string out = disassemble(chunk, "while");
    REQUIRE(out.find("Loop") != std::string::npos);
    REQUIRE(out.find("-> 0") != std::string::npos);
}

TEST_CASE("a loop body too long to jump back over is a compile error", "[compiler]") {
    // same two-byte operand as a forward jump, so the same 65535 ceiling. the
    // Loop is written before the exit jump is patched, so this is the one that
    // gives up first.
    std::string source = "while 1 {\n";
    for (int i = 0; i < 25000; ++i) {
        // Const, its pool index and the Pop, three bytes a line.
        source += "1\n";
    }
    source += "}";

    REQUIRE_THROWS_AS(compile_source(source), CompileError);
}

TEST_CASE("a fn declaration compiles into a function with its own chunk", "[compiler]") {
    // the outer chunk only pushes the function and names it, the same two
    // instructions a top level `let` would use.
    Chunk chunk = compile_source("fn f(a, b) {}");
    REQUIRE(chunk.size() == 5);
    REQUIRE(op_at(chunk, 2) == Opcode::DefineGlobal);
    REQUIRE(std::get<std::string>(chunk.constant_at(chunk.code[3])) == "f");
    REQUIRE(op_at(chunk, 4) == Opcode::Return);

    std::shared_ptr<CompiledFn> fn = fn_at(chunk, 0);
    REQUIRE(fn->name == "f");
    REQUIRE(fn->arity == 2);
    REQUIRE(fn->upvalue_count == 0);

    // an empty body is still a chunk the VM can stop in.
    REQUIRE(fn->chunk.size() == 1);
    REQUIRE(op_at(fn->chunk, 0) == Opcode::Return);
}

TEST_CASE("a fn with no params has arity 0", "[compiler]") {
    Chunk chunk = compile_source("fn main() {}");
    REQUIRE(fn_at(chunk, 0)->arity == 0);
}

TEST_CASE("the body ends up in the function's chunk and not the outer one", "[compiler]") {
    Chunk chunk = compile_source("fn f() { 1 + 2 }");
    for (std::size_t i = 0; i < chunk.size(); i += instruction_size(chunk, i)) {
        REQUIRE(op_at(chunk, i) != Opcode::Add);
    }

    const Chunk& body = fn_at(chunk, 0)->chunk;
    REQUIRE(op_at(body, 4) == Opcode::Add);
    REQUIRE(op_at(body, 5) == Opcode::Pop);
    REQUIRE(op_at(body, 6) == Opcode::Return);

    // the body's literals go in the body's pool too, the outer one only has the
    // function and its name.
    REQUIRE(chunk.constants.size() == 2);
    REQUIRE(body.constants.size() == 2);
}

TEST_CASE("params are locals starting at slot 1", "[compiler]") {
    // slot 0 is saved for the function itself, which is where step 82 will have
    // it sitting on the stack below the arguments.
    Chunk chunk = compile_source("fn f(a, b) { a b }");
    const Chunk& body = fn_at(chunk, 0)->chunk;
    REQUIRE(op_at(body, 0) == Opcode::GetLocal);
    REQUIRE(body.code[1] == 1);
    REQUIRE(op_at(body, 3) == Opcode::GetLocal);
    REQUIRE(body.code[4] == 2);
}

TEST_CASE("a let inside a fn body is a local and not a global", "[compiler]") {
    Chunk chunk = compile_source("fn f(a) { let x = 1 x }");
    const Chunk& body = fn_at(chunk, 0)->chunk;
    for (std::size_t i = 0; i < body.size(); i += instruction_size(body, i)) {
        REQUIRE(op_at(body, i) != Opcode::DefineGlobal);
    }

    // a took slot 1, so x lands on 2.
    REQUIRE(op_at(body, 2) == Opcode::GetLocal);
    REQUIRE(body.code[3] == 2);
}

TEST_CASE("a name that isn't a param is looked up as a global", "[compiler]") {
    Chunk chunk = compile_source("fn f(a) { print }");
    const Chunk& body = fn_at(chunk, 0)->chunk;
    REQUIRE(op_at(body, 0) == Opcode::GetGlobal);
    REQUIRE(std::get<std::string>(body.constant_at(body.code[1])) == "print");
}

TEST_CASE("a param doesn't leak out into the code after the fn", "[compiler]") {
    // the body had its own Compiler, so `a` was never in the outer locals and
    // reading it afterwards is a global.
    Chunk chunk = compile_source("fn f(a) { a }\na");
    REQUIRE(op_at(chunk, 4) == Opcode::GetGlobal);
}

TEST_CASE("a fn inside a block is bound to a local slot", "[compiler]") {
    // Const, then nothing, since the value on the stack is the local. end_scope
    // pops it on the way out like any other.
    Chunk chunk = compile_source("{ fn g() {} }");
    REQUIRE(chunk.size() == 4);
    REQUIRE(fn_at(chunk, 0)->name == "g");
    REQUIRE(op_at(chunk, 2) == Opcode::Pop);
    REQUIRE(op_at(chunk, 3) == Opcode::Return);
}

TEST_CASE("compiling a fn body doesn't mess up the outer block's slots", "[compiler]") {
    // a is slot 0 and g is slot 1 out here, and x is slot 1 in there. if the two
    // shared one locals_ list the read of a afterwards would come out wrong.
    Chunk chunk = compile_source("{ let a = 1 fn g(x) { x } a }");
    REQUIRE(op_at(chunk, 4) == Opcode::GetLocal);
    REQUIRE(chunk.code[5] == 0);

    const Chunk& body = fn_at(chunk, 2)->chunk;
    REQUIRE(op_at(body, 0) == Opcode::GetLocal);
    REQUIRE(body.code[1] == 1);
}

TEST_CASE("two fns with the same body still get a constant each", "[compiler]") {
    Chunk chunk = compile_source("fn a() {}\nfn b() {}");
    REQUIRE(fn_at(chunk, 0)->name == "a");
    REQUIRE(fn_at(chunk, 4)->name == "b");
    REQUIRE(chunk.code[1] != chunk.code[5]);
}

TEST_CASE("the function's instructions carry the lines from its body", "[compiler]") {
    Chunk chunk = compile_source("fn f() {\n\n1\n}");
    const Chunk& body = fn_at(chunk, 0)->chunk;
    REQUIRE(body.line_at(0) == 3);
    REQUIRE(chunk.line_at(0) == 1);
}

TEST_CASE("255 params is fine but 256 is a compile error", "[compiler]") {
    // slot 0 plus the params all have to fit in a one-byte slot number.
    auto fn_with_params = [](int count) {
        std::string source = "fn f(";
        for (int i = 0; i < count; ++i) {
            if (i > 0) source += ", ";
            source += "p" + std::to_string(i);
        }
        return source + ") {}";
    };

    REQUIRE(fn_at(compile_source(fn_with_params(255)), 0)->arity == 255);
    REQUIRE_THROWS_AS(compile_source(fn_with_params(256)), CompileError);
}

TEST_CASE("an error inside a fn body stops the whole compile", "[compiler]") {
    std::string source = "fn f() {\n";
    for (int i = 0; i < 300; ++i) {
        source += std::to_string(i) + "\n";
    }
    source += "}";
    REQUIRE_THROWS_AS(compile_source(source), CompileError);
}

TEST_CASE("running a fn declaration binds the function as a global", "[compiler]") {
    // nothing calls it yet, this only checks the value gets there in one piece.
    VM vm;
    run_source(vm, "fn add(a, b) { a + b }");
    REQUIRE(vm.stack_size() == 0);

    const Value* add = vm.global("add");
    REQUIRE(add != nullptr);
    REQUIRE(is_compiled_fn(*add));
    REQUIRE(std::get<std::shared_ptr<CompiledFn>>(*add)->arity == 2);
    REQUIRE(to_string(*add) == "<fn add>");
}

TEST_CASE("the disassembler shows a compiled fn by name", "[compiler]") {
    Chunk chunk = compile_source("fn greet() {}");
    std::string out = disassemble(chunk, "fn");
    REQUIRE(out.find("'<fn greet>'") != std::string::npos);
}

TEST_CASE("a call emits the callee, then the arguments, then Call", "[compiler]") {
    // that order is the calling convention and not just one that happens to
    // work: the VM makes the callee slot 0 of the frame and the arguments the
    // slots above it, so anything emitted out of order lands in the wrong slot.
    Chunk chunk = compile_source("fn f(a, b) {} f(1, 2)");

    // Const <fn>, DefineGlobal f, then the call statement.
    REQUIRE(op_at(chunk, 4) == Opcode::GetGlobal);
    REQUIRE(op_at(chunk, 6) == Opcode::Const);
    REQUIRE(op_at(chunk, 8) == Opcode::Const);
    REQUIRE(op_at(chunk, 10) == Opcode::Call);
    REQUIRE(chunk.code[11] == 2);
}

TEST_CASE("a call with no arguments still carries its count", "[compiler]") {
    Chunk chunk = compile_source("fn f() {} f()");
    REQUIRE(op_at(chunk, 6) == Opcode::Call);
    REQUIRE(chunk.code[7] == 0);
}

TEST_CASE("the argument values are pushed left to right", "[compiler]") {
    // the pool holds them in the order they were compiled, so reading the two
    // Const operands back shows which side went first.
    Chunk chunk = compile_source("fn f(a, b) {} f(10, 20)");
    REQUIRE(std::get<int64_t>(chunk.constant_at(chunk.code[7])) == 10);
    REQUIRE(std::get<int64_t>(chunk.constant_at(chunk.code[9])) == 20);
}

TEST_CASE("a call statement drops the value the call leaves", "[compiler]") {
    // every expression statement ends in a Pop, and a call is no different — the
    // VM pushes whatever the function returned and nothing here wants it.
    Chunk chunk = compile_source("fn f() {} f()");
    REQUIRE(op_at(chunk, 8) == Opcode::Pop);
}

TEST_CASE("calling a local goes through GetLocal", "[compiler]") {
    // the callee is compiled like any other expression, so it resolves the same
    // way a bare mention of the name would.
    Chunk chunk = compile_source("{ fn f() {} f() }");
    const Chunk& body = chunk;
    bool has_get_local = false;
    for (std::size_t i = 0; i < body.size(); i += instruction_size(body, i)) {
        if (op_at(body, i) == Opcode::GetLocal) has_get_local = true;
    }
    REQUIRE(has_get_local);
}

TEST_CASE("the callee can be any expression", "[compiler]") {
    // `make()()` needs nothing special: the inner call leaves one value on the
    // stack like everything else and the outer Call takes it from there.
    Chunk chunk = compile_source("fn make() {} make()()");
    int calls = 0;
    for (std::size_t i = 0; i < chunk.size(); i += instruction_size(chunk, i)) {
        if (op_at(chunk, i) == Opcode::Call) ++calls;
    }
    REQUIRE(calls == 2);
}

TEST_CASE("a call is recorded against the line its arguments open on", "[compiler]") {
    // the opening paren, which is the token the interpreter blames a bad call on
    // as well. a call broken over two lines is reported on the first of them.
    Chunk chunk = compile_source("fn f(a) {}\nf(\n1)");
    std::size_t offset = 0;
    for (std::size_t i = 0; i < chunk.size(); i += instruction_size(chunk, i)) {
        if (op_at(chunk, i) == Opcode::Call) offset = i;
    }
    REQUIRE(chunk.line_at(offset) == 2);
}

TEST_CASE("255 arguments is fine but 256 is a compile error", "[compiler]") {
    // the count rides in one operand byte, same limit the parameter side runs
    // into from the other direction.
    auto call_with_args = [](int count) {
        std::string source = "fn f() {} f(";
        for (int i = 0; i < count; ++i) {
            if (i > 0) source += ", ";
            source += "1";
        }
        return source + ")";
    };

    REQUIRE_NOTHROW(compile_source(call_with_args(255)));
    REQUIRE_THROWS_AS(compile_source(call_with_args(256)), CompileError);
}

TEST_CASE("a compiled call runs the function it names", "[compiler]") {
    // the whole way through for once: source to bytecode to a frame the VM runs.
    // the body writes to a global because its Return stops the VM where it
    // stands until step 83, so there is nothing left on the stack to look at.
    VM vm;
    run_source(vm, "let out = 0\nfn f(a) { out = a }\nf(7)");

    REQUIRE(vm.global("out") != nullptr);
    REQUIRE(std::get<int64_t>(*vm.global("out")) == 7);
}

TEST_CASE("a compiled call sees its parameters in the right order", "[compiler]") {
    VM vm;
    run_source(vm, "let out = 0\nfn f(a, b) { out = a - b }\nf(10, 4)");
    REQUIRE(std::get<int64_t>(*vm.global("out")) == 6);
}

TEST_CASE("a local in a fn body sits above the parameters", "[compiler]") {
    // the body's own locals are counted from the frame base like the params are,
    // so a `let` inside a function lands in the slot after the last one.
    VM vm;
    run_source(vm, "let out = 0\nfn f(a) { let twice = a + a out = twice }\nf(6)");
    REQUIRE(std::get<int64_t>(*vm.global("out")) == 12);
}
