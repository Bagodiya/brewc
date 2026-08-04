#include "brewc/repl.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "brewc/ast.h"
#include "brewc/ast_printer.h"
#include "brewc/interpreter.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/runtime_error.h"
#include "brewc/source_snippet.h"
#include "brewc/token.h"
#include "brewc/value.h"

namespace brewc {

namespace {

// drain the lexer into a flat token list the same way the parser tests do, all
// the way through the End marker so the parser knows where the line stops.
std::vector<Token> lex_line(const std::string& line) {
    Lexer lexer(line);
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.next_token();
        bool last = tok.kind == TokenKind::End;
        tokens.push_back(std::move(tok));
        if (last) {
            break;
        }
    }
    return tokens;
}

// dump the parsed tree instead of running it. this is what the whole repl used to
// do, kept behind `:ast` because it's still the quickest way to see how something
// grouped when a precedence question comes up.
void print_ast(const std::vector<std::unique_ptr<Stmt>>& program) {
    AstPrinter printer;
    for (const auto& stmt : program) {
        std::cout << printer.print(*stmt) << "\n";
    }
}

// run one parsed line. an expression on its own gets its value printed, since
// typing `1 + 2` and being shown nothing is the one thing a repl mustn't do.
// every other statement runs quietly — a `let` or an `fn` has already said what
// it does by existing, and echoing it would just be noise.
//
// nil is the exception even for expressions: `print(x)` does its own output and
// then comes back as nil, and following that with a bare "nil" line reads like
// something went wrong when nothing did.
void run_line(Interpreter& interp, std::vector<std::unique_ptr<Stmt>>& program) {
    for (auto& stmt : program) {
        if (auto* expr_stmt = dynamic_cast<ExprStmt*>(stmt.get())) {
            Value value = interp.evaluate(*expr_stmt->expr);
            if (!is_nil(value)) {
                std::cout << to_string(value) << "\n";
            }
            continue;
        }
        interp.execute(*stmt);
    }
}

// every tree the session has parsed so far, one entry per line that made it past
// the parser. this exists purely to keep those trees alive: a function value
// holds a bare FnDecl* into the tree it was declared in, on the assumption that
// the parsed program outlives the run. that holds for a file, where one tree
// covers the whole program, but a repl parses a fresh tree per line — and letting
// that tree die at the end of the line would leave every `fn` defined on it
// pointing at freed memory the moment it got called.
//
// nothing is ever removed. a session's worth of one-line ASTs is a rounding error
// next to keeping a repl honest.
using AstArena = std::vector<std::vector<std::unique_ptr<Stmt>>>;

// lex + parse one line and either run it or show the tree. blank lines just get
// skipped so hitting enter on an empty prompt doesn't spew anything.
//
// a runtime error is reported and then swallowed rather than ending the session.
// the interpreter is untouched by that, so everything already bound is still
// bound and the line can just be typed again with the mistake fixed.
void eval_line(Interpreter& interp, AstArena& arena, const std::string& line,
               bool show_ast) {
    if (line.empty()) {
        return;
    }

    Parser parser(lex_line(line));
    auto program = parser.parse_program();

    if (!parser.errors().empty()) {
        for (const auto& err : parser.errors()) {
            std::cout << "error: " << err.what() << "\n";
            // the line the user just typed is the whole "source" here, so the
            // caret lands under the right column without any extra bookkeeping.
            std::string snippet = source_snippet(line, err.line(), err.column());
            if (!snippet.empty()) {
                std::cout << snippet << "\n";
            }
        }
        return;
    }

    if (show_ast) {
        // nothing runs in this mode, so no function value can capture a pointer
        // into this tree and it's free to go at the end of the call.
        print_ast(program);
        return;
    }

    // hand the tree to the arena before running it, so anything the line declares
    // is pointing at storage that outlives the line.
    arena.push_back(std::move(program));

    try {
        run_line(interp, arena.back());
    } catch (const RuntimeError& err) {
        std::cout << format_error(err, line) << "\n";
    }
}

} // namespace

int run_repl() {
    std::cout << "brewc repl — :help for commands, :quit or ctrl-d to leave\n";

    // one interpreter for the whole session, so a name defined on one line is
    // still bound on the next. building a fresh one per line would make `let x =
    // 1` followed by `x` an undefined variable error.
    Interpreter interp;
    // outlives every line, for the reason spelled out on AstArena above.
    AstArena arena;
    bool show_ast = false;

    std::string line;
    while (true) {
        std::cout << "brew> ";
        if (!std::getline(std::cin, line)) {
            // ctrl-d / EOF. drop a newline so the shell prompt doesn't end up
            // glued to the last "brew> " we printed.
            std::cout << "\n";
            break;
        }

        if (line == ":quit") {
            break;
        }
        if (line == ":help") {
            std::cout << "  :ast    toggle showing the parsed tree instead of running it\n";
            std::cout << "  :quit   leave the repl\n";
            continue;
        }
        if (line == ":ast") {
            show_ast = !show_ast;
            std::cout << (show_ast ? "showing parsed trees\n" : "running lines\n");
            continue;
        }

        eval_line(interp, arena, line, show_ast);
    }
    return 0;
}

} // namespace brewc
