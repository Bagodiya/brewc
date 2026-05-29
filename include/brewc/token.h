#ifndef BREWC_TOKEN_H
#define BREWC_TOKEN_H

#include <string>

namespace brewc {

enum class TokenKind {
    // literals
    Integer,
    Float,
    String,
    Identifier,

    // keywords
    Let,
    Fn,
    If,
    Else,
    While,
    Return,
    True,
    False,
    Nil,

    // arithmetic
    Plus,
    Minus,
    Star,
    Slash,
    Percent,

    // comparison
    EqualEqual,
    BangEqual,
    Less,
    Greater,
    LessEqual,
    GreaterEqual,

    // logical
    AmpAmp,
    PipePipe,
    Bang,

    // assignment
    Equal,

    // punctuation
    LParen,
    RParen,
    LBrace,
    RBrace,
    Comma,
    Semicolon,

    // markers
    End,
    Error,
};

struct Token {
    TokenKind kind;
    std::string lexeme;
    int line;
    int column;

    Token(TokenKind k, std::string text, int ln, int col)
        : kind(k), lexeme(std::move(text)), line(ln), column(col) {}
};

} // namespace brewc

#endif
