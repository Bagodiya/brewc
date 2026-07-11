#include "brewc/value.h"

#include <sstream>

#include "brewc/ast.h"

namespace brewc {

bool is_nil(const Value& value) { return std::holds_alternative<Nil>(value); }
bool is_int(const Value& value) { return std::holds_alternative<int64_t>(value); }
bool is_float(const Value& value) { return std::holds_alternative<double>(value); }
bool is_bool(const Value& value) { return std::holds_alternative<bool>(value); }
bool is_string(const Value& value) { return std::holds_alternative<std::string>(value); }
bool is_function(const Value& value) { return std::holds_alternative<Function>(value); }
bool is_native(const Value& value) {
    return std::holds_alternative<std::shared_ptr<NativeFn>>(value);
}

std::string type_name(const Value& value) {
    if (is_nil(value)) return "nil";
    if (is_int(value)) return "int";
    if (is_float(value)) return "float";
    if (is_bool(value)) return "bool";
    // a builtin is a callable just like a user fn, so from a brew program's point
    // of view it's the same "function" type.
    if (is_function(value) || is_native(value)) return "function";
    return "string";
}

std::string to_string(const Value& value) {
    if (is_nil(value)) return "nil";
    if (is_int(value)) return std::to_string(std::get<int64_t>(value));
    if (is_bool(value)) return std::get<bool>(value) ? "true" : "false";
    if (is_string(value)) return std::get<std::string>(value);

    // a function prints as its name so `print(add)` shows something readable
    // instead of blowing up. needs the ast include above to reach decl->name.
    if (is_function(value)) {
        const FnDecl* decl = std::get<Function>(value).decl;
        return "<fn " + (decl ? decl->name : "?") + ">";
    }

    // builtins print much the same way, tagged so they're easy to tell apart from
    // a function written in brew itself.
    if (is_native(value)) {
        const auto& native = std::get<std::shared_ptr<NativeFn>>(value);
        return "<builtin " + (native ? native->name : "?") + ">";
    }

    // float left. std::to_string would pad it out to six decimals which looks
    // ugly, so stream it instead and then make sure there's a dot in there so it
    // still reads as a float. skip the patch-up for things like inf/nan/1e10
    // that already don't look like an int.
    double number = std::get<double>(value);
    std::ostringstream out;
    out << number;
    std::string text = out.str();
    bool looks_like_float = text.find('.') != std::string::npos ||
                            text.find('e') != std::string::npos ||
                            text.find('n') != std::string::npos; // inf / nan
    if (!looks_like_float) {
        text += ".0";
    }
    return text;
}

} // namespace brewc
