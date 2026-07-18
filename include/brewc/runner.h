#ifndef BREWC_RUNNER_H
#define BREWC_RUNNER_H

#include <string>

namespace brewc {

// read a .brew file off disk and run the whole thing: lex, parse, interpret.
// this is what `brewc run <file>` ends up calling. returns 0 when the program
// finished cleanly, and non-zero if the file wouldn't open, the parser found
// something wrong, or the program hit a runtime error while running. all the
// error text goes to stderr so the program's own print output stays clean.
int run_file(const std::string& path);

} // namespace brewc

#endif
