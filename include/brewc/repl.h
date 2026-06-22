#ifndef BREWC_REPL_H
#define BREWC_REPL_H

namespace brewc {

// read-eval-print loop for poking at the front end by hand. right now there's no
// interpreter yet, so all "eval" does is lex + parse the line and dump the AST as
// an S-expression. keeps a tight feedback loop while the parser is still moving.
// returns the exit code so main can just forward it.
int run_repl();

} // namespace brewc

#endif
