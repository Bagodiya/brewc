# brewc

A tree-walking interpreter for Brew, a small dynamically-typed language, written
in C++17 with no dependencies outside the standard library.

```
fn make_counter() {
    let count = 0
    fn tick() {
        count = count + 1
        return count
    }
    return tick
}

let next = make_counter()
print(next())   // 1
print(next())   // 2
```

## Build

Needs CMake 3.16+ and a C++17 compiler (Clang 10+, GCC 9+, MSVC 2019+).

```sh
cmake -B build
cmake --build build
```

## Use

```sh
./build/brewc                       # start the repl
./build/brewc run examples/fib.brew # run a program
./build/brewc version
```

The repl keeps its bindings between lines and prints the value of anything you
type that's an expression. `:ast` switches it to showing the parsed tree instead
of running it, which is the quickest way to settle a precedence question:

```
brew> let x = 21
brew> x * 2
42
brew> :ast
showing parsed trees
brew> 1 + 2 * 3
(+ 1 (* 2 3))
```

## The language

Integers, floats, strings, booleans and `nil`. `let` bindings, `=` assignment,
`if`/`else`, `while`, and block scoping. Arithmetic, comparison and logical
operators with the usual precedence, plus unary `-` and `!`. `+` concatenates
strings, and an int is widened to a float when the two meet in one expression.

Functions are values. They can be passed around, returned, and they close over
the scope they were declared in — which is what makes the counter above work.
`print` and `clock` are built in.

[`docs/spec.md`](docs/spec.md) is the grammar and the full description.
[`docs/interpreter.md`](docs/interpreter.md) covers how the interpreter works
inside, and is the file to read before changing it.

Errors point at the exact spot, and runtime errors come with a stack trace:

```
runtime error: division by zero (line 2, column 19)
  2 | fn b() { return 1 / 0 }
    |                   ^
stack trace:
  in b() called from line 1
  in a() called from line 3
```

## Tests

```sh
ctest --test-dir build --output-on-failure
```

394 test cases under Catch2, covering each stage on its own plus end-to-end runs
of every program in [`examples/`](examples/). GitHub Actions builds and runs them
on every push.

## Status

The interpreter is complete and is what runs today. A bytecode compiler and VM
are being built alongside it — the opcode set, chunk format, constant pool and
disassembler are done and tested, and the compiler currently handles literals,
arithmetic and comparisons. It isn't wired into the driver yet.

Not there yet: arrays, structs, modules, and a garbage collector.

## License

MIT. See [LICENSE](LICENSE).
