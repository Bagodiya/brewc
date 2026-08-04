#ifndef BREWC_REPL_H
#define BREWC_REPL_H

namespace brewc {

// read-eval-print loop. each line is lexed, parsed and run through the same
// Interpreter a file gets, and one interpreter is kept alive for the whole
// session so a binding made on one line is still there on the next.
//
// a line that is just an expression prints the value it came out as, which is
// the part that makes a repl worth using; statements run for their effect and
// print nothing extra. `:ast` toggles dumping the parsed tree instead of running
// it — that's all this used to do, and it's still the quickest way to check how
// something parses. returns the exit code so main can just forward it.
int run_repl();

} // namespace brewc

#endif
