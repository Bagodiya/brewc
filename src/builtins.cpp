#include "brewc/builtins.h"

#include <chrono>
#include <cstddef>
#include <iostream>
#include <string>

namespace brewc {

namespace {

// args separated by one space, then a newline. returns nil.
Value builtin_print(const std::vector<Value>& args) {
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (i != 0) {
            std::cout << ' ';
        }
        std::cout << to_string(args[i]);
    }
    std::cout << '\n';
    return Nil{};
}

// seconds off steady_clock. the zero point means nothing, only the
// difference between two calls does. args are ignored.
Value builtin_clock(const std::vector<Value>& args) {
    (void)args;
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    double seconds = std::chrono::duration<double>(now).count();
    return seconds;
}

std::shared_ptr<NativeFn> make_native(const std::string& name, Value (*fn)(const std::vector<Value>&)) {
    auto native = std::make_shared<NativeFn>();
    native->name = name;
    native->call = fn;
    return native;
}

} // namespace

const std::vector<std::shared_ptr<NativeFn>>& builtins() {
    static const std::vector<std::shared_ptr<NativeFn>> all = {
        make_native("print", builtin_print),
        make_native("clock", builtin_clock),
    };
    return all;
}

} // namespace brewc
