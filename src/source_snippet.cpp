#include "brewc/source_snippet.h"

#include <cstddef>
#include <string>

namespace brewc {

namespace {

// grab line number `line` (1-based) out of the source, without the newline on
// the end. found is set to false when the file just doesn't have that many
// lines, which happens if an error points past the last one.
std::string line_text(const std::string& source, int line, bool& found) {
    found = false;
    if (line < 1) {
        return "";
    }

    int current = 1;
    std::size_t start = 0;
    while (start <= source.size()) {
        std::size_t end = source.find('\n', start);
        if (end == std::string::npos) {
            end = source.size();
        }
        if (current == line) {
            found = true;
            std::string text = source.substr(start, end - start);
            // windows line endings leave a stray \r behind, and that would push
            // the caret one column off, so drop it.
            if (!text.empty() && text.back() == '\r') {
                text.pop_back();
            }
            return text;
        }
        if (end == source.size()) {
            break;
        }
        current++;
        start = end + 1;
    }
    return "";
}

} // namespace

std::string source_snippet(const std::string& source, int line, int column) {
    if (source.empty() || line < 1 || column < 1) {
        return "";
    }

    bool found = false;
    std::string text = line_text(source, line, found);
    if (!found) {
        return "";
    }

    // the gutter is "  <n> | ", and the caret line needs the same width of blank
    // space in front of its own bar so the two line up in the terminal.
    std::string number = std::to_string(line);
    std::string gutter = "  " + number + " | ";
    std::string pad(gutter.size() - 2, ' ');

    std::string caret_line = pad + "| ";
    // step across the source line up to the column we want. tabs get copied over
    // as tabs so a tab-indented line still puts the caret in the right place.
    std::size_t stop = static_cast<std::size_t>(column - 1);
    if (stop > text.size()) {
        stop = text.size();
    }
    for (std::size_t i = 0; i < stop; i++) {
        caret_line += (text[i] == '\t') ? '\t' : ' ';
    }
    caret_line += '^';

    return gutter + text + "\n" + caret_line;
}

} // namespace brewc
