# Changelog

## Unreleased

### Fixed

- `-x` and `!x` evaluated to whatever the previous expression had left behind
  instead of to the negation. `visit_unary` was an empty stub, so this failed
  silently rather than erroring — `3 + -2` came out as `6`. Both operators are
  implemented now, and negating a non-number is a proper error.
- `//` comments were in the spec but the lexer never skipped them, so a
  commented line was a syntax error.

### Language

- Expression statements. A bare `print(x)` is a statement now, so the examples no
  longer smuggle calls in through a `let _ =` binding nobody reads.
- `=` assignment, as an expression, so `a = b = 0` works and a `while` loop can
  drive its own counter. Assigning to a name that was never bound is an error
  rather than a quiet new global.
- `return`, with or without a value. A function evaluates to what it returned, or
  `nil` if it ran to the end without one. This is what makes functions
  composable: `factorial` builds its answer on the way back up instead of
  printing at the bottom of the recursion, and a function can now be returned
  from another function and called later.
- `&&` and `||` are evaluated, and they short-circuit — the right side is only
  reached when the left didn't already settle the answer. They parsed before but
  hit a "cannot apply" error at runtime.
- A `{ ... }` block can stand on its own as a statement, which is how you keep a
  `let` from leaking into the rest of the program.
- A trailing `;` is accepted after `let`, `return` and expression statements.

### Tooling

- The repl runs what you type. It used to only lex, parse and print the tree.
  Bindings persist across lines, an expression prints its value, and errors are
  reported without ending the session. `:ast` brings back the old tree-dumping
  behaviour, `:help` lists the commands.
- 394 tests, up from 278.

## v0.1.0

First tagged release. Everything up to here is the tree-walking interpreter;
the bytecode VM is next.

### Language

- Integer, float, string and bool literals, with escape sequences in strings
- `let` bindings, `if`/`else`, `while` loops and block scoping
- Arithmetic and comparison operators with the usual precedence, grouping with
  parens
- `fn` declarations, calls, recursion and closures over the enclosing scope
- `+` concatenates strings
- Ints promote to floats when the two are mixed in one expression
- `print` and `clock` builtins

### Tooling

- `brewc` starts a REPL, `brewc run <file>` executes a program
- `brewc version` prints the version
- Syntax and runtime errors report a line, a column and a caret under the
  offending token; runtime errors also print a stack trace
- 278 tests under Catch2, including end-to-end runs of the files in `examples/`
- GitHub Actions builds and runs the test suite on every push

### Known gaps

- Unary `-` and `!` parse but don't evaluate, and `&&` and `||` fail at runtime
- No expression statements, no assignment, no `return`
- The REPL prints the parsed tree rather than running it
- No arrays, structs or modules yet
- No garbage collector, values are copied
- `fib(30)` on the tree-walker is slow, that's what the VM in v0.2.0 is for
