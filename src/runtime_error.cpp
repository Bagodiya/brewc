#include "brewc/runtime_error.h"

#include <string>

namespace brewc {

std::string format_error(const RuntimeError& err) {
    std::string out = "runtime error: ";
    out += err.what();

    // line 0 means we never got a real source position for this one, so skip the
    // "at line" bit rather than print a bogus zero.
    if (err.line() > 0) {
        out += " (line " + std::to_string(err.line());
        if (err.column() > 0) {
            out += ", column " + std::to_string(err.column());
        }
        out += ")";
    }

    const auto& trace = err.trace();
    if (!trace.empty()) {
        out += "\nstack trace:";
        // walk it back to front so the call we were deepest inside prints first,
        // the same order you'd read a backtrace in any other language.
        for (auto it = trace.rbegin(); it != trace.rend(); ++it) {
            out += "\n  in " + it->fn_name + "() called from line " +
                   std::to_string(it->call_line);
        }
    }

    return out;
}

} // namespace brewc
