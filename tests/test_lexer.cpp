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
