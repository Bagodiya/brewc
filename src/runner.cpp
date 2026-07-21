#include "brewc/runner.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "brewc/interpreter.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/runtime_error.h"
#include "brewc/source_snippet.h"
#include "brewc/token.h"

namespace brewc {

namespace {

// run the lexer to exhaustion and collect every token, up to and including the
// End marker so the parser knows where the source stops. the repl and the tests
// do the same walk; a file just feeds the whole thing in at once instead of a
// line at a time.
std::vector<Token> lex_all(const std::string& source) {
    Lexer lexer(source);
    std::vector<Token> tokens;
    while (true) {
        Token tok = lexer.next_token();
        bool done = tok.kind == TokenKind::End;
        tokens.push_back(std::move(tok));
        if (done) {
            break;
        }
    }
    return tokens;
}

// print an error message and, when we know where it came from, the source line
// with a caret under the spot. the snippet comes back empty for positions we
// can't place, and then this is just the message on its own.
void report(const std::string& message, const std::string& source, int line,
            int column) {
    std::cerr << message << "\n";
    std::string snippet = source_snippet(source, line, column);
    if (!snippet.empty()) {
        std::cerr << snippet << "\n";
    }
}

} // namespace

int run_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "brewc: could not open '" << path << "'\n";
        return 1;
    }

    // slurp the whole file into one string. nothing here is big enough to bother
    // streaming it, and the lexer wants the full source anyway.
    std::ostringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    Parser parser(lex_all(source));
    auto program = parser.parse_program();

    // if the parser recovered from anything, the tree is only partial, so bail
    // before running it instead of half-executing a broken file.
    if (!parser.errors().empty()) {
        for (const auto& err : parser.errors()) {
            report(std::string("error: ") + err.what(), source, err.line(),
                   err.column());
        }
        return 1;
    }

    Interpreter interp;
    try {
        interp.interpret(program);
    } catch (const RuntimeError& err) {
        // this overload already tucks the caret line in under the message, so it
        // goes straight out instead of through report().
        std::cerr << format_error(err, source) << "\n";
        return 1;
    }
    return 0;
}

} // namespace brewc
