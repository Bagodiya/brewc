#include <catch2/catch_test_macros.hpp>

#include <string>

#include "brewc/source_snippet.h"

using namespace brewc;

TEST_CASE("snippet points at a column on the first line", "[snippet]") {
    std::string source = "let x = 1;\n";
    std::string out = source_snippet(source, 1, 5);
    REQUIRE(out == "  1 | let x = 1;\n    |     ^");
}

TEST_CASE("snippet picks the right line out of a longer file", "[snippet]") {
    std::string source = "let a = 1;\nlet b = 2;\nlet c = 3;\n";
    std::string out = source_snippet(source, 2, 1);
    REQUIRE(out == "  2 | let b = 2;\n    | ^");
}

TEST_CASE("the caret line follows a wider line number", "[snippet]") {
    // a two digit line number makes the gutter one char wider, and the bar on
    // the caret line has to move over with it.
    std::string source;
    for (int i = 1; i <= 12; i++) {
        source += "let x = " + std::to_string(i) + ";\n";
    }
    std::string out = source_snippet(source, 12, 9);
    REQUIRE(out == "  12 | let x = 12;\n     |         ^");
}

TEST_CASE("snippet copies tabs so the caret stays under the column",
          "[snippet]") {
    std::string source = "\tlet x = 1;\n";
    std::string out = source_snippet(source, 1, 2);
    REQUIRE(out == "  1 | \tlet x = 1;\n    | \t^");
}

TEST_CASE("snippet handles the last line without a trailing newline",
          "[snippet]") {
    std::string source = "let a = 1;\nlet b = 2;";
    std::string out = source_snippet(source, 2, 5);
    REQUIRE(out == "  2 | let b = 2;\n    |     ^");
}

TEST_CASE("snippet drops a windows carriage return", "[snippet]") {
    std::string source = "let x = 1;\r\nlet y = 2;\r\n";
    std::string out = source_snippet(source, 1, 5);
    REQUIRE(out == "  1 | let x = 1;\n    |     ^");
}

TEST_CASE("snippet is empty when the position is not real", "[snippet]") {
    std::string source = "let x = 1;\n";
    REQUIRE(source_snippet(source, 9, 1).empty());
    REQUIRE(source_snippet(source, 0, 1).empty());
    REQUIRE(source_snippet(source, 1, 0).empty());
    REQUIRE(source_snippet("", 1, 1).empty());
}

TEST_CASE("a column past the end of the line still gets a caret", "[snippet]") {
    // the parser reports "expected ;" at the position right after the last
    // token, which can sit one past the end of the text.
    std::string source = "let x = 1\n";
    std::string out = source_snippet(source, 1, 20);
    REQUIRE(out == "  1 | let x = 1\n    |          ^");
}
