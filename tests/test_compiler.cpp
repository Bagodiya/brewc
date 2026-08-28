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

// compile a whole program and run it. everything else in this file reads the
// bytes back, but a Pop is only worth anything for what the stack looks like when
// the program finishes, and the chunk on its own can't show that.
void run_source(VM& vm, const std::string& source) {
    Chunk chunk = compile_source(source);
    REQUIRE(vm.run(chunk) == InterpretResult::Ok);
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
    // if, while and fn are still empty visitors, so nothing here reaches down
    // into the statements inside them — the `let` in each body never gets
    // compiled. all this really checks is that the walk gets to every node and
    // comes back.
    REQUIRE(compile_source("if 1 < 2 { let y = 3 }").size() == 1);
    REQUIRE(compile_source("while 1 { let z = 2 }").size() == 1);
    REQUIRE(compile_source("fn add(a, b) { let c = a + b }").size() == 1);
}

TEST_CASE("nested blocks are walked all the way down", "[compiler]") {
    Chunk chunk = compile_source("fn outer(n) { while n { if n { let deep = 1 } } }");
    REQUIRE(chunk.size() == 1);
    REQUIRE(op_at(chunk, 0) == Opcode::Return);
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

TEST_CASE("an operator with no opcode yet is reported, not skipped", "[compiler]") {
    // && and || can't be a plain instruction — they short-circuit, which needs a
    // jump over the right operand, and jumps aren't in yet. until then they have
    // to fail loudly: emitting the operands and no operator would leave two
    // values on the stack and the VM would carry on with the wrong one.
    REQUIRE_THROWS_AS(compile_expr_source("1 && 2"), CompileError);
    REQUIRE_THROWS_AS(compile_expr_source("1 || 2"), CompileError);
}

TEST_CASE("a compile error says which line and column it came from", "[compiler]") {
    CompileError err(4, 9, "too many constants in one chunk");
    REQUIRE(err.line() == 4);
    REQUIRE(err.column() == 9);
    REQUIRE(std::string(err.what()) == "line 4:9: too many constants in one chunk");
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
