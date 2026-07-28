# Changelog

## v0.1.0

First tagged release. Everything up to here is the tree-walking interpreter;
the bytecode VM is next.

### Language

- Integer, float, string and bool literals, with escape sequences in strings
- `let` bindings, `if`/`else`, `while` loops and block scoping
- Arithmetic, comparison and logical operators with the usual precedence
- Unary `-` and `!`, grouping with parens
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

- No arrays, structs or modules yet
- No garbage collector, values are copied
- `fib(30)` on the tree-walker is slow, that's what the VM in v0.2.0 is for
