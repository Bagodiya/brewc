#ifndef BREWC_RUNTIME_ERROR_H
#define BREWC_RUNTIME_ERROR_H

#include <stdexcept>
#include <string>
#include <vector>

namespace brewc {

// one line of the call stack. fn_name is the function that was running and
// call_line is the source line where the call that got us into it was written,
// so a trace reads "we blew up inside count, which was called from line 8".
struct TraceFrame {
    std::string fn_name;
    int call_line;
};

// thrown when a program does something the interpreter can't carry out at run
// time: dividing by zero, reading a name that was never bound, calling a value
// that isn't a function, and so on. it still derives from std::runtime_error so
// the older tests that only catch that base type keep working, but on top of the
// message it also remembers where in the source the trouble was and the chain of
// calls that were in flight when it happened.
//
// the base what() stays just the plain message on purpose — the location and the
// stack live in their own fields so a caller decides how much of it to show.
class RuntimeError : public std::runtime_error {
public:
    RuntimeError(const std::string& message, int line, int column,
                 std::vector<TraceFrame> trace)
        : std::runtime_error(message), line_(line), column_(column),
          trace_(std::move(trace)) {}

    int line() const { return line_; }
    int column() const { return column_; }
    const std::vector<TraceFrame>& trace() const { return trace_; }

private:
    int line_;
    int column_;
    std::vector<TraceFrame> trace_;
};

// turn a RuntimeError into the multi-line report a user actually sees: the
// message with its source position, then the call stack underneath it, innermost
// call first. a program that errors at the top level has an empty trace, so in
// that case there's just the one line and no stack section.
std::string format_error(const RuntimeError& err);

} // namespace brewc

#endif
