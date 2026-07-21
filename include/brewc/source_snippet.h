#ifndef BREWC_SOURCE_SNIPPET_H
#define BREWC_SOURCE_SNIPPET_H

#include <string>

namespace brewc {

// build the little two-line block that shows where an error happened:
//
//    3 | let x = 1 +;
//      |            ^
//
// line and column are both 1-based, the same way the lexer counts them. if the
// position doesn't land anywhere real (line past the end of the file, column
// zero, empty source) this gives back an empty string so the caller can just
// print the plain message and move on.
std::string source_snippet(const std::string& source, int line, int column);

} // namespace brewc

#endif
