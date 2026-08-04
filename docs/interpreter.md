# How the interpreter works

Notes on the tree-walking interpreter in `src/interpreter.cpp`. `docs/spec.md`
covers what the language looks like; this one covers what happens after parsing,
so it's the file to read before touching interpreter code.

The whole thing is deliberately simple: the parser hands back an AST and we walk
it, running each node as we reach it. This is the path everything actually runs
through today. A bytecode compiler is being built alongside it in
`src/compiler.cpp`, but it isn't finished and nothing in the driver calls it yet.

## The pipeline

    source text -> Lexer -> tokens -> Parser -> AST -> Interpreter -> output

`Interpreter::interpret` takes the vector of top-level statements the parser
produced and executes them one after another. Nothing is checked ahead of time,
so an error in a function body only shows up when that function actually gets
called.

## Walking the tree

`Interpreter` implements both `Visitor` (expressions) and `StmtVisitor`
(statements), because the tree mixes the two. Every `visit_*` method returns
void, which is a problem for expressions since they need to produce a value. The
way around it is a single member, `result_`: a `visit_*` writes its answer there
and `evaluate()` reads it back out.

    Value Interpreter::evaluate(Expr& expr) {
        expr.accept(*this);
        return result_;
    }

So `evaluate(expr)` is the one to call for expressions, and `execute(stmt)` for
statements. Nothing outside the class should be poking at `result_` directly.

One thing worth knowing: `result_` is a single shared slot, so it only holds the
*most recent* result. If a visit method evaluates two subexpressions, it has to
copy the first one into a local before evaluating the second, otherwise the
second overwrites it. `visit_binary` does exactly that.

## Values

`Value` (in `include/brewc/value.h`) is a `std::variant` over everything a brew
expression can produce:

| Alternative               | Brew type                  |
| ------------------------- | -------------------------- |
| `Nil`                     | `nil`                      |
| `int64_t`                 | integer                    |
| `double`                  | float                      |
| `bool`                    | boolean                    |
| `std::string`             | string                     |
| `Function`                | a `fn` the user declared   |
| `std::shared_ptr<NativeFn>` | a builtin like `print`   |

`Nil` is an empty struct rather than a null pointer so it's just another
alternative and the `is_*` helpers can treat it like every other case.

Builtins sit behind a `shared_ptr` on purpose. `NativeFn` holds a
`std::function<Value(const std::vector<Value>&)>`, which mentions `Value`, so it
can't be a complete type at the point the variant is declared. The pointer breaks
that cycle.

The order of the alternatives lines up with the `is_*` helpers below the variant,
so don't reorder them.

## Scopes

An `Environment` is a hashmap from name to `Value` plus a pointer to the scope it
sits inside. Lookup checks the local map first and then walks outward along the
parent chain until it finds the name or runs off the top.

`define` always binds in the current scope, which is how an inner `let` shadows an
outer variable without disturbing it. `assign` walks outward looking for a binding
that already exists and refuses to create one, since inventing variables is
`define`'s job. That split is what makes `x = 1` inside a block write to the outer
`x` while `let x = 1` in the same spot shadows it instead, and it's why assigning
to a name nobody bound is an error rather than a quiet new global — a typo in an
assignment should say so.

The interpreter holds two of these: `globals_` for the outermost scope, which
lives for the whole run, and `current_` for whatever scope we're in right now. A
block points `current_` at a fresh child scope, runs, and points it back:

    auto inner = std::make_shared<Environment>(current_);
    std::shared_ptr<Environment> outer = current_;
    current_ = inner;
    try {
        // ... run the statements ...
    } catch (...) {
        current_ = outer;
        throw;
    }
    current_ = outer;

The try/catch is not decoration. Without it, a statement that throws partway
through leaves `current_` aimed at a scope that's about to be destroyed, and
whoever catches the error is holding a dangling interpreter. `visit_call` does the
same save/restore for the same reason.

Both the parent pointer and `globals_` are `shared_ptr`, not raw pointers. That's
what makes closures work — see below.

## Functions and closures

`visit_fn` doesn't run anything. It builds a `Function{&stmt, current_}` and binds
it under the function's name, exactly like `let` binds a number. Storing it as an
ordinary value is what makes functions first class: you can look one up, pass it
around, and call it later.

The two fields do different jobs:

- `decl` points back at the `FnDecl` AST node, so the body, params and name are
  still reachable when the call finally happens. It's a plain pointer because the
  parsed program owns every node and outlives the run.
- `closure` is the scope the function was *declared* in, held by `shared_ptr`.

That second one is the interesting half. A call hangs its parameter scope off
`closure`, not off whoever happened to make the call. Two things fall out of that:

Recursion works, because the function's own name was bound in its declaring
scope, so the body can find it. `examples/factorial.brew` leans on that. And
closures work, because holding a `shared_ptr` to the declaring scope keeps that
scope alive for as long as the function value is around.
`examples/closure_counter.brew` is the case for that one: the inner `tick` reads
`base`, which belongs to the `make_counter` call scope, and each call to
`make_counter` gets its own `base` that the `tick` declared inside it keeps
hold of.

A call goes: evaluate the callee, evaluate all the arguments left to right,
check it's actually callable, check the argument count, build the call scope, run
the body, restore. Arguments are all evaluated up front so a later argument can't
observe a half-built call scope.

Builtins skip most of that. They have no AST body to walk, so `visit_call` hands
the argument vector straight to the C++ callback and takes whatever comes back.

## Return

A `return` can be nested any number of blocks, ifs and loops deep, and all of that
has to be abandoned at once to get the value back to the call. Checking a flag
after every statement would mean touching `visit_block`, `visit_if` and
`visit_while` and getting all three right; throwing gets the same job done with
the unwinding the language already has.

`visit_return` evaluates the value and throws a `ReturnSignal` holding it.
`visit_call` catches it right where the body was run, which is what stops it at
that call — otherwise an inner function returning would keep unwinding and return
out of its caller too. A body that finishes without throwing evaluates to `nil`.

`ReturnSignal` deliberately doesn't derive from `std::exception`, let alone from
`RuntimeError`. It isn't a failure, and keeping it off that hierarchy means
nothing that reports errors can mistake it for one. The `catch (...)` blocks that
restore `current_` still see it and put the scope back on the way past, which is
exactly what should happen; they just re-throw it afterwards.

A `return` at the top level has no call waiting to catch it, so `visit_return`
checks for an empty `call_stack_` and fails with a real error instead. Letting it
through would unwind straight out of `interpret` and end the run with nothing
printed.

## The REPL and node lifetimes

Worth knowing if you touch `src/repl.cpp`: a `Function` holds a bare `FnDecl*`,
which is only safe because the parsed program outlives the run. A file gets one
tree for the whole program, so that holds by itself. The REPL parses a *new* tree
per line, and dropping it at the end of the line would leave every `fn` declared
on it pointing at freed memory the next time it was called. So the REPL keeps
every line's tree in an arena that lives as long as the session does.

## Truthiness

Only `nil` and a false boolean are falsy. Everything else is truthy, including
`0` and the empty string. Keeping the rule that small means there's nothing to
memorize, and it's the rule `if`, `while`, `!`, `&&` and `||` all use.

`&&` and `||` are handled at the very top of `visit_binary`, before it evaluates
anything. They have to be: every other operator evaluates both sides up front,
and these two don't always evaluate the right one. That isn't a speed trick — it's
what lets `n != 0 && total / n > 1` use its left side to guard its right. Both
reduce to a bool rather than to whichever operand won, matching the comparisons,
which always hand back a bool whatever they were given.

## Equality and ordering

Any two numbers compare with all six operators, including an int against a float.
Everything else only gets `==` and `!=`, and only values of the exact same kind
can be equal, so a bool is never equal to a string. Trying to order two strings is
a runtime error rather than something with a made-up answer.

## Numbers and promotion

Two ints stay ints: `7 / 2` is `3` and `%` works. As soon as one side is a float
the int gets widened to a double and the float rules take over, so `7 / 2.0` is
`3.5` and `1 + 2.5` is `3.5`. A mixed result is always a float even when it lands
on a whole number — `1 + 1.0` is `2.0`, since handing back an int there would
quietly drop the `.0` the user wrote. `%` is the one operator that doesn't go
along with this; there's no sensible float modulo, so it stays an error.

Two ints also compare as ints rather than through doubles, which keeps large
values exact. Past 2^53 an `int64_t` holds more precision than a `double` does,
so a huge int does lose its low bits when a float drags it into the mix. That's
the same trade every language with a single float type makes.

## Errors

Anything the interpreter can't carry out at run time throws a `RuntimeError`:
dividing by zero, reading an unbound name, calling something that isn't a
function, wrong argument count.

Every failure goes through `Interpreter::fail`, which stamps the message with the
source position of the token that caused it and takes a **copy** of `call_stack_`.
The copy is the whole point. As the exception unwinds, each `visit_call` catch
block pops its frame, so by the time anyone catches the error the live stack is
empty. Grabbing it at throw time is the only chance to keep the chain.

`call_stack_` itself is a vector of `TraceFrame{fn_name, call_line}`, oldest
first, pushed before a body runs and popped after. `format_error` turns all of
that into the report a user sees: the message with its position, then the stack
underneath, innermost call first. A program that fails at the top level has an
empty trace and gets just the one line.

`RuntimeError` still derives from `std::runtime_error`, and `what()` is kept as
the plain message with no location glued on, so callers decide how much to show.

The small helpers at the top of the file (`apply_int`, `compare`, and friends)
throw a bare `std::runtime_error` with just a message, since they have no idea
where in the source they are. `visit_binary` catches those and re-throws through
`fail` with the operator token, which is where the position and stack get
attached. The subexpression `evaluate` calls happen *outside* that try on
purpose, so an error from deeper in the tree keeps its own position instead of
being stamped with this operator's.

## Builtins

Registered into the globals by `register_builtins()` in the constructor, so a
program can call them without declaring anything.

`print` writes its arguments separated by spaces followed by a newline, using
`to_string`, which already knows how to render every variant. It returns nil.

`clock` returns seconds as a float for timing: read it before and after some work
and subtract. It uses `steady_clock` rather than the wall clock, which can jump
when the system time is adjusted; the zero point is arbitrary but only the gap
between two readings ever matters.

## Known gaps

- No arrays, structs or modules.
- No garbage collector. Values are copied, and a scope lives as long as something
  holds a `shared_ptr` to it, so a cycle between two closures would never be
  freed.
- No user-visible way to define a builtin.
- The tree-walker re-visits every node on each pass through a loop and pays for
  the virtual dispatch each time. That's what the bytecode compiler under
  `src/compiler.cpp` is for; it isn't wired into the driver yet.
