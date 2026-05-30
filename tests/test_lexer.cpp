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
