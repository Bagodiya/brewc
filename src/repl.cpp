#include "brewc/repl.h"

#include <iostream>
#include <string>
#include <vector>

#include "brewc/ast.h"
#include "brewc/ast_printer.h"
#include "brewc/lexer.h"
#include "brewc/parser.h"
#include "brewc/token.h"

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

// lex + parse one line and print the result. blank lines just get skipped so
// hitting enter on an empty prompt doesn't spew anything.
void eval_line(const std::string& line) {
    if (line.empty()) {
        return;
    }

    Parser parser(lex_line(line));
    auto program = parser.parse_program();

    if (!parser.errors().empty()) {
        for (const auto& err : parser.errors()) {
            std::cout << "error: " << err.what() << "\n";
        }
        return;
    }

    AstPrinter printer;
    for (const auto& stmt : program) {
        std::cout << printer.print(*stmt) << "\n";
    }
}

} // namespace

int run_repl() {
    std::cout << "brewc repl — type :quit or hit ctrl-d to leave\n";

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
        eval_line(line);
    }
    return 0;
}

} // namespace brewc
