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

### Statement terminators

Statements are separated by line breaks, not by semicolons. A trailing `;` is
accepted after a `let`, an expression statement or a `return` and is ignored, so
both of these are the same program:

    let x = 10
    let x = 10;

## Syntax

### Variable Bindings

`let` introduces a new binding in the current scope. Assigning to a name that is
already bound uses `=` on its own, which updates the existing binding rather than
making a second one — assigning to a name that was never bound is an error.

    let x = 10
    let name = "brewc"
    x = 11

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

A function returns the value of its `return`, or `nil` if it runs to the end
without one. A bare `return` with no expression is also `nil`. Functions are
values: they can be passed to other functions and returned from them, and a
function returned this way keeps the scope it was declared in alive.

    fn add(a, b) {
        return a + b
    }

    fn make_counter() {
        let count = 0
        fn tick() {
            count = count + 1
            return count
        }
        return tick
    }

### Truthiness

Only `nil` and `false` are falsy. Everything else is truthy, including `0` and
the empty string. `if`, `while` and `!` all use this same rule.

`&&` and `||` short-circuit: the right side is only evaluated if the left side
didn't already settle the answer. Both reduce to a boolean rather than to one of
their operands, so `1 && 2` is `true`, not `2`.

## Grammar

Whitespace and `//` comments are discarded by the lexer and do not appear in
the grammar below.

    program     := stmt*
    stmt        := let_stmt | fn_decl | if_stmt | while_stmt | return_stmt | expr_stmt | block
    let_stmt    := "let" IDENT "=" expr ";"?
    fn_decl     := "fn" IDENT "(" params? ")" block
    if_stmt     := "if" expr block ("else" (if_stmt | block))?
    while_stmt  := "while" expr block
    return_stmt := "return" expr? ";"?
    expr_stmt   := expr ";"?
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
    params      := IDENT ("," IDENT)*
    args        := expr ("," expr)*
