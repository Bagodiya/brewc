#include <catch2/catch_test_macros.hpp>

#include "brewc/lexer.h"

using namespace brewc;

TEST_CASE("lexer scans a single integer", "[lexer]") {
    Lexer lex("42");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::Integer);
    REQUIRE(t.lexeme == "42");
}

TEST_CASE("lexer skips surrounding whitespace", "[lexer]") {
    Lexer lex("   7  ");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::Integer);
    REQUIRE(t.lexeme == "7");

    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("lexer scans two integers in a row", "[lexer]") {
    Lexer lex("10 200");

    Token first = lex.next_token();
    REQUIRE(first.kind == TokenKind::Integer);
    REQUIRE(first.lexeme == "10");

    Token second = lex.next_token();
    REQUIRE(second.kind == TokenKind::Integer);
    REQUIRE(second.lexeme == "200");

    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("lexer scans a float literal", "[lexer]") {
    Lexer lex("3.14");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::Float);
    REQUIRE(t.lexeme == "3.14");

    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("trailing dot is not part of the number", "[lexer]") {
    // "1." should give back the integer 1 and leave the dot behind
    Lexer lex("1.");
    Token num = lex.next_token();
    REQUIRE(num.kind == TokenKind::Integer);
    REQUIRE(num.lexeme == "1");

    // the dot on its own isn't a valid token yet
    REQUIRE(lex.next_token().kind == TokenKind::Error);
}

TEST_CASE("leading dot is not a float", "[lexer]") {
    Lexer lex(".5");

    // nothing before the dot, so it can't start a number
    REQUIRE(lex.next_token().kind == TokenKind::Error);

    Token num = lex.next_token();
    REQUIRE(num.kind == TokenKind::Integer);
    REQUIRE(num.lexeme == "5");
}

TEST_CASE("two dots only consume the first float", "[lexer]") {
    Lexer lex("1.2.3");

    Token f = lex.next_token();
    REQUIRE(f.kind == TokenKind::Float);
    REQUIRE(f.lexeme == "1.2");

    REQUIRE(lex.next_token().kind == TokenKind::Error); // the second dot

    Token last = lex.next_token();
    REQUIRE(last.kind == TokenKind::Integer);
    REQUIRE(last.lexeme == "3");
}

TEST_CASE("lexer scans a plain identifier", "[lexer]") {
    Lexer lex("counter");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::Identifier);
    REQUIRE(t.lexeme == "counter");

    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("identifiers can hold underscores and digits", "[lexer]") {
    Lexer lex("_x1 my_var2");

    Token first = lex.next_token();
    REQUIRE(first.kind == TokenKind::Identifier);
    REQUIRE(first.lexeme == "_x1");

    Token second = lex.next_token();
    REQUIRE(second.kind == TokenKind::Identifier);
    REQUIRE(second.lexeme == "my_var2");
}

TEST_CASE("lexer recognises every keyword", "[lexer]") {
    Lexer lex("let fn if else while return true false nil");

    REQUIRE(lex.next_token().kind == TokenKind::Let);
    REQUIRE(lex.next_token().kind == TokenKind::Fn);
    REQUIRE(lex.next_token().kind == TokenKind::If);
    REQUIRE(lex.next_token().kind == TokenKind::Else);
    REQUIRE(lex.next_token().kind == TokenKind::While);
    REQUIRE(lex.next_token().kind == TokenKind::Return);
    REQUIRE(lex.next_token().kind == TokenKind::True);
    REQUIRE(lex.next_token().kind == TokenKind::False);
    REQUIRE(lex.next_token().kind == TokenKind::Nil);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("a word that only starts with a keyword is still an identifier", "[lexer]") {
    // "letter" begins with "let" but shouldn't be treated as the keyword
    Lexer lex("letter ifx");

    Token first = lex.next_token();
    REQUIRE(first.kind == TokenKind::Identifier);
    REQUIRE(first.lexeme == "letter");

    Token second = lex.next_token();
    REQUIRE(second.kind == TokenKind::Identifier);
    REQUIRE(second.lexeme == "ifx");
}

TEST_CASE("a name that starts with a digit is not an identifier", "[lexer]") {
    // 2nd splits into the integer 2 and the identifier nd
    Lexer lex("2nd");

    Token num = lex.next_token();
    REQUIRE(num.kind == TokenKind::Integer);
    REQUIRE(num.lexeme == "2");

    Token name = lex.next_token();
    REQUIRE(name.kind == TokenKind::Identifier);
    REQUIRE(name.lexeme == "nd");
}

TEST_CASE("lexer scans the arithmetic operators", "[lexer]") {
    Lexer lex("+ - * / %");

    REQUIRE(lex.next_token().kind == TokenKind::Plus);
    REQUIRE(lex.next_token().kind == TokenKind::Minus);
    REQUIRE(lex.next_token().kind == TokenKind::Star);
    REQUIRE(lex.next_token().kind == TokenKind::Slash);
    REQUIRE(lex.next_token().kind == TokenKind::Percent);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("lexer scans punctuation", "[lexer]") {
    Lexer lex("( ) { } , ;");

    REQUIRE(lex.next_token().kind == TokenKind::LParen);
    REQUIRE(lex.next_token().kind == TokenKind::RParen);
    REQUIRE(lex.next_token().kind == TokenKind::LBrace);
    REQUIRE(lex.next_token().kind == TokenKind::RBrace);
    REQUIRE(lex.next_token().kind == TokenKind::Comma);
    REQUIRE(lex.next_token().kind == TokenKind::Semicolon);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("operators glued to numbers still split apart", "[lexer]") {
    // no spaces here, the lexer should still pull out each piece
    Lexer lex("1+2");

    Token one = lex.next_token();
    REQUIRE(one.kind == TokenKind::Integer);
    REQUIRE(one.lexeme == "1");

    REQUIRE(lex.next_token().kind == TokenKind::Plus);

    Token two = lex.next_token();
    REQUIRE(two.kind == TokenKind::Integer);
    REQUIRE(two.lexeme == "2");
}

TEST_CASE("operator lexeme holds the actual character", "[lexer]") {
    Lexer lex("*");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::Star);
    REQUIRE(t.lexeme == "*");
}

TEST_CASE("lexer scans the two-character operators", "[lexer]") {
    Lexer lex("== != <= >= && ||");

    REQUIRE(lex.next_token().kind == TokenKind::EqualEqual);
    REQUIRE(lex.next_token().kind == TokenKind::BangEqual);
    REQUIRE(lex.next_token().kind == TokenKind::LessEqual);
    REQUIRE(lex.next_token().kind == TokenKind::GreaterEqual);
    REQUIRE(lex.next_token().kind == TokenKind::AmpAmp);
    REQUIRE(lex.next_token().kind == TokenKind::PipePipe);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("single = < > ! fall back to one-char tokens", "[lexer]") {
    Lexer lex("= < > !");

    REQUIRE(lex.next_token().kind == TokenKind::Equal);
    REQUIRE(lex.next_token().kind == TokenKind::Less);
    REQUIRE(lex.next_token().kind == TokenKind::Greater);
    REQUIRE(lex.next_token().kind == TokenKind::Bang);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("two-char operator lexeme keeps both characters", "[lexer]") {
    Lexer lex("<=");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::LessEqual);
    REQUIRE(t.lexeme == "<=");
}

TEST_CASE("a lone & or | is an error", "[lexer]") {
    // we don't have bitwise operators, so a single & by itself is junk
    Lexer amp("&");
    REQUIRE(amp.next_token().kind == TokenKind::Error);

    Lexer pipe("|");
    REQUIRE(pipe.next_token().kind == TokenKind::Error);
}

TEST_CASE("comparison glued to operands still splits apart", "[lexer]") {
    Lexer lex("a==b");

    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::EqualEqual);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("assignment next to comparison reads correctly", "[lexer]") {
    // "x = y == z" mixes a single = with a double ==
    Lexer lex("x = y == z");

    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::Equal);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::EqualEqual);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("lexer scans a plain string", "[lexer]") {
    Lexer lex("\"hello\"");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::String);
    REQUIRE(t.lexeme == "hello");

    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("string escapes are turned into real characters", "[lexer]") {
    // \n and \t should become an actual newline and tab in the value
    Lexer lex("\"a\\nb\\tc\"");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::String);
    REQUIRE(t.lexeme == "a\nb\tc");
}

TEST_CASE("escaped quote and backslash stay inside the string", "[lexer]") {
    // the string is:  say "hi" \  (a quote pair and a trailing backslash)
    Lexer lex("\"say \\\"hi\\\" \\\\\"");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::String);
    REQUIRE(t.lexeme == "say \"hi\" \\");
}

TEST_CASE("empty string is valid", "[lexer]") {
    Lexer lex("\"\"");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::String);
    REQUIRE(t.lexeme == "");

    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("a string sitting between other tokens", "[lexer]") {
    Lexer lex("let s = \"yo\"");

    REQUIRE(lex.next_token().kind == TokenKind::Let);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::Equal);

    Token str = lex.next_token();
    REQUIRE(str.kind == TokenKind::String);
    REQUIRE(str.lexeme == "yo");

    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("a small call-like sequence tokenizes cleanly", "[lexer]") {
    Lexer lex("foo(a, b)");

    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::LParen);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::Comma);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::RParen);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("first token starts at line 1 column 1", "[lexer]") {
    Lexer lex("x");
    Token t = lex.next_token();
    REQUIRE(t.line == 1);
    REQUIRE(t.column == 1);
}

TEST_CASE("column advances along a single line", "[lexer]") {
    // "a b" -> a at col 1, b at col 3 (the space takes up col 2)
    Lexer lex("a b");

    Token a = lex.next_token();
    REQUIRE(a.line == 1);
    REQUIRE(a.column == 1);

    Token b = lex.next_token();
    REQUIRE(b.line == 1);
    REQUIRE(b.column == 3);
}

TEST_CASE("a newline bumps the line and resets the column", "[lexer]") {
    Lexer lex("a\nb");

    Token a = lex.next_token();
    REQUIRE(a.line == 1);
    REQUIRE(a.column == 1);

    Token b = lex.next_token();
    REQUIRE(b.line == 2);
    REQUIRE(b.column == 1);
}

TEST_CASE("line keeps climbing across several newlines", "[lexer]") {
    Lexer lex("1\n2\n  3");

    REQUIRE(lex.next_token().line == 1);

    Token two = lex.next_token();
    REQUIRE(two.line == 2);
    REQUIRE(two.column == 1);

    // two leading spaces on the third line, so 3 lands at column 3
    Token three = lex.next_token();
    REQUIRE(three.line == 3);
    REQUIRE(three.column == 3);
}

TEST_CASE("location points at the start of a multi-char token", "[lexer]") {
    Lexer lex("  hello");
    Token t = lex.next_token();
    REQUIRE(t.kind == TokenKind::Identifier);
    REQUIRE(t.column == 3); // skips the two spaces and points at the 'h'
}

TEST_CASE("empty input is just End", "[lexer]") {
    Lexer lex("");
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("input that's only whitespace gives End", "[lexer]") {
    Lexer lex("   \t \n  ");
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("asking past the end keeps handing back End", "[lexer]") {
    // once we're done we shouldn't fall off the source, every extra call is End
    Lexer lex("1");
    REQUIRE(lex.next_token().kind == TokenKind::Integer);
    REQUIRE(lex.next_token().kind == TokenKind::End);
    REQUIRE(lex.next_token().kind == TokenKind::End);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("tabs and carriage returns count as whitespace", "[lexer]") {
    Lexer lex("a\t\rb");

    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("a minus in front of a number is its own token", "[lexer]") {
    // we don't do negative literals in the lexer, -5 is minus then 5
    Lexer lex("-5");

    REQUIRE(lex.next_token().kind == TokenKind::Minus);

    Token n = lex.next_token();
    REQUIRE(n.kind == TokenKind::Integer);
    REQUIRE(n.lexeme == "5");
}

TEST_CASE("column lands right after a two-char operator", "[lexer]") {
    // "== x" -> the == sits at cols 1-2, the space is col 3, x at col 4
    Lexer lex("== x");

    Token op = lex.next_token();
    REQUIRE(op.kind == TokenKind::EqualEqual);
    REQUIRE(op.column == 1);

    Token x = lex.next_token();
    REQUIRE(x.kind == TokenKind::Identifier);
    REQUIRE(x.column == 4);
}

TEST_CASE("a whole little function tokenizes in order", "[lexer]") {
    // run a realistic snippet through the lexer and check the full stream
    Lexer lex("fn add(a, b) { return a + b; }");

    REQUIRE(lex.next_token().kind == TokenKind::Fn);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::LParen);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::Comma);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::RParen);
    REQUIRE(lex.next_token().kind == TokenKind::LBrace);
    REQUIRE(lex.next_token().kind == TokenKind::Return);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::Plus);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::Semicolon);
    REQUIRE(lex.next_token().kind == TokenKind::RBrace);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("a condition with mixed literals and operators", "[lexer]") {
    Lexer lex("if x >= 3.5 && y != nil");

    REQUIRE(lex.next_token().kind == TokenKind::If);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::GreaterEqual);

    Token f = lex.next_token();
    REQUIRE(f.kind == TokenKind::Float);
    REQUIRE(f.lexeme == "3.5");

    REQUIRE(lex.next_token().kind == TokenKind::AmpAmp);
    REQUIRE(lex.next_token().kind == TokenKind::Identifier);
    REQUIRE(lex.next_token().kind == TokenKind::BangEqual);
    REQUIRE(lex.next_token().kind == TokenKind::Nil);
    REQUIRE(lex.next_token().kind == TokenKind::End);
}

TEST_CASE("location is right for tokens spread over two lines", "[lexer]") {
    Lexer lex("let x = 1\nx = x + 2");

    REQUIRE(lex.next_token().line == 1); // let
    REQUIRE(lex.next_token().line == 1); // x
    REQUIRE(lex.next_token().line == 1); // =
    REQUIRE(lex.next_token().line == 1); // 1

    Token second_x = lex.next_token();
    REQUIRE(second_x.kind == TokenKind::Identifier);
    REQUIRE(second_x.line == 2);
    REQUIRE(second_x.column == 1);
}
