#include "brewc/runtime_error.h"

#include <string>

#include "brewc/source_snippet.h"

namespace brewc {

namespace {

// the first line of a report: what went wrong and where.
std::string error_head(const RuntimeError& err) {
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
    return out;
}

// the call stack section, or nothing at all when the error happened at the top
// level and there was no call in flight.
std::string error_trace(const RuntimeError& err) {
    const auto& trace = err.trace();
    if (trace.empty()) {
        return "";
    }

    std::string out = "\nstack trace:";
    // walk it back to front so the call we were deepest inside prints first,
    // the same order you'd read a backtrace in any other language.
    for (auto it = trace.rbegin(); it != trace.rend(); ++it) {
        out += "\n  in " + it->fn_name + "() called from line " +
               std::to_string(it->call_line);
    }
    return out;
}

} // namespace

std::string format_error(const RuntimeError& err) {
    return error_head(err) + error_trace(err);
}

std::string format_error(const RuntimeError& err, const std::string& source) {
    std::string out = error_head(err);

    std::string snippet = source_snippet(source, err.line(), err.column());
    if (!snippet.empty()) {
        out += "\n" + snippet;
    }

    return out + error_trace(err);
}

} // namespace brewc
