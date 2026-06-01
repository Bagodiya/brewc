# Brew Language Specification

Version 0.1 (draft)

## Overview

Brew is a small dynamically-typed programming language. This document describes
its lexical structure and syntax.

## Lexical Structure

### Comments

Single-line comments begin with `//` and continue to the end of the line.

### Identifiers

An identifier begins with a letter or underscore, followed by zero or more
letters, digits, or underscores.

### Keywords

    let   fn   if   else   while   return   true   false   nil

### Literals

| Kind    | Examples            |
| ------- | ------------------- |
| Integer | `0`, `42`           |
| Float   | `3.14`, `0.5`       |
| String  | `"hello"`, `"a\nb"` |
| Boolean | `true`, `false`     |
| Nil     | `nil`               |

### String escapes

Inside a string literal the following escape sequences are recognized:

| Escape | Meaning      |
| ------ | ------------ |
| `\n`   | newline      |
| `\t`   | tab          |
| `\"`   | double quote |
| `\\`   | backslash    |

Any other character after a backslash is kept as-is. A string that reaches the
end of input before its closing quote is a lexical error.

### Operators

| Category   | Operators                   |
| ---------- | --------------------------- |
| Arithmetic | `+` `-` `*` `/` `%`         |
| Comparison | `==` `!=` `<` `>` `<=` `>=` |
| Logical    | `&&` `\|\|` `!`             |
| Assignment | `=`                         |

## Syntax

### Variable Bindings

    let x = 10
    let name = "brewc"

### Conditionals

    if x > 0 {
        print("positive")
    } else {
        print("non-positive")
    }

### Loops

    while i < 10 {
        i = i + 1
    }

### Functions

    fn add(a, b) {
        return a + b
    }

## Grammar

    program     := stmt*
    stmt        := let_stmt | fn_decl | if_stmt | while_stmt | return_stmt | expr_stmt | block
    let_stmt    := "let" IDENT "=" expr
    fn_decl     := "fn" IDENT "(" params? ")" block
    if_stmt     := "if" expr block ("else" block)?
    while_stmt  := "while" expr block
    return_stmt := "return" expr?
    block       := "{" stmt* "}"
    expr        := assignment
    assignment  := IDENT "=" expr | logic_or
    logic_or    := logic_and ("||" logic_and)*
    logic_and   := equality ("&&" equality)*
    equality    := comparison (("==" | "!=") comparison)*
    comparison  := term (("<" | ">" | "<=" | ">=") term)*
    term        := factor (("+" | "-") factor)*
    factor      := unary (("*" | "/" | "%") unary)*
    unary       := ("!" | "-") unary | call
    call        := primary ("(" args? ")")*
    primary     := INT | FLOAT | STRING | "true" | "false" | "nil" | IDENT | "(" expr ")"
