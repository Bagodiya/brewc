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
