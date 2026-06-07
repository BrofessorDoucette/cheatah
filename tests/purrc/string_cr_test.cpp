// Compile-run unit tests for the `string` module: one test per function. Each
// writes a tiny .purr that calls a single string function, compiles it with
// purrc, runs it under the cheatah runtime, and asserts the exact stdout.
// Complements the in-process unit tests (stdlib/tests/string_test.cpp) and the
// per-module system-level test (StdlibE2E.String).
//
// List-returning functions (split/splitlines) are joined back into a single
// deterministic string before printing. Booleans print as True/False.
#include "e2e_harness.hpp"

TEST(StringCompileRun, Upper) {
    e2e::expect_e2e("string_upper", R"PURR(import io
import string
io.print(string.upper("meow"))
)PURR", "MEOW\n");
}

TEST(StringCompileRun, Lower) {
    e2e::expect_e2e("string_lower", R"PURR(import io
import string
io.print(string.lower("MeOw"))
)PURR", "meow\n");
}

TEST(StringCompileRun, Capitalize) {
    e2e::expect_e2e("string_capitalize", R"PURR(import io
import string
io.print(string.capitalize("hello world"))
)PURR", "Hello world\n");
}

TEST(StringCompileRun, Title) {
    e2e::expect_e2e("string_title", R"PURR(import io
import string
io.print(string.title("hello world"))
)PURR", "Hello World\n");
}

TEST(StringCompileRun, Swapcase) {
    e2e::expect_e2e("string_swapcase", R"PURR(import io
import string
io.print(string.swapcase("Meow"))
)PURR", "mEOW\n");
}

TEST(StringCompileRun, Strip) {
    e2e::expect_e2e("string_strip", R"PURR(import io
import string
io.print(string.strip("  meow \t"))
)PURR", "meow\n");
}

TEST(StringCompileRun, Lstrip) {
    e2e::expect_e2e("string_lstrip", R"PURR(import io
import string
io.print(string.lstrip("xxmeow", "x"))
)PURR", "meow\n");
}

TEST(StringCompileRun, Rstrip) {
    e2e::expect_e2e("string_rstrip", R"PURR(import io
import string
io.print(string.rstrip("meowyy", "y"))
)PURR", "meow\n");
}

TEST(StringCompileRun, Startswith) {
    e2e::expect_e2e("string_startswith", R"PURR(import io
import string
io.print(string.startswith("meow", "me"))
)PURR", "True\n");
}

TEST(StringCompileRun, Endswith) {
    e2e::expect_e2e("string_endswith", R"PURR(import io
import string
io.print(string.endswith("meow", "ow"))
)PURR", "True\n");
}

TEST(StringCompileRun, Contains) {
    e2e::expect_e2e("string_contains", R"PURR(import io
import string
io.print(string.contains("meow", "eo"))
)PURR", "True\n");
}

TEST(StringCompileRun, Find) {
    e2e::expect_e2e("string_find", R"PURR(import io
import string
io.print(string.find("meow meow", "meow"))
)PURR", "0\n");
}

TEST(StringCompileRun, Rfind) {
    e2e::expect_e2e("string_rfind", R"PURR(import io
import string
io.print(string.rfind("meow meow", "meow"))
)PURR", "5\n");
}

TEST(StringCompileRun, Count) {
    e2e::expect_e2e("string_count", R"PURR(import io
import string
io.print(string.count("meow meow meow", "meow"))
)PURR", "3\n");
}

TEST(StringCompileRun, Replace) {
    e2e::expect_e2e("string_replace", R"PURR(import io
import string
io.print(string.replace("meow meow", "e", "3"))
)PURR", "m3ow m3ow\n");
}

TEST(StringCompileRun, Split) {
    e2e::expect_e2e("string_split", R"PURR(import io
import string
io.print(string.join("|", string.split("a,b,c", ",")))
)PURR", "a|b|c\n");
}

TEST(StringCompileRun, SplitWhitespace) {
    e2e::expect_e2e("string_split_ws", R"PURR(import io
import string
io.print(string.join("|", string.split("  a   b ")))
)PURR", "a|b\n");
}

TEST(StringCompileRun, Splitlines) {
    e2e::expect_e2e("string_splitlines", R"PURR(import io
import string
io.print(string.join("|", string.splitlines("a\nb\nc")))
)PURR", "a|b|c\n");
}

TEST(StringCompileRun, Capwords) {
    e2e::expect_e2e("string_capwords", R"PURR(import io
import string
io.print(string.capwords("the quick brown"))
)PURR", "The Quick Brown\n");
}

TEST(StringCompileRun, Join) {
    e2e::expect_e2e("string_join", R"PURR(import io
import string
io.print(string.join("-", ["a", "b", "c"]))
)PURR", "a-b-c\n");
}

TEST(StringCompileRun, Ljust) {
    e2e::expect_e2e("string_ljust", R"PURR(import io
import string
io.print(string.ljust("cat", 5))
)PURR", "cat  \n");
}

TEST(StringCompileRun, Rjust) {
    e2e::expect_e2e("string_rjust", R"PURR(import io
import string
io.print(string.rjust("cat", 5))
)PURR", "  cat\n");
}

TEST(StringCompileRun, Center) {
    e2e::expect_e2e("string_center", R"PURR(import io
import string
io.print(string.center("cat", 9, "*"))
)PURR", "***cat***\n");
}

TEST(StringCompileRun, Zfill) {
    e2e::expect_e2e("string_zfill", R"PURR(import io
import string
io.print(string.zfill("-42", 5))
)PURR", "-0042\n");
}

TEST(StringCompileRun, Isdigit) {
    e2e::expect_e2e("string_isdigit", R"PURR(import io
import string
io.print(string.isdigit("123"))
)PURR", "True\n");
}

TEST(StringCompileRun, Isalpha) {
    e2e::expect_e2e("string_isalpha", R"PURR(import io
import string
io.print(string.isalpha("abc"))
)PURR", "True\n");
}

TEST(StringCompileRun, Isalnum) {
    e2e::expect_e2e("string_isalnum", R"PURR(import io
import string
io.print(string.isalnum("abc123"))
)PURR", "True\n");
}

TEST(StringCompileRun, Isspace) {
    e2e::expect_e2e("string_isspace", R"PURR(import io
import string
io.print(string.isspace("  \t\n"))
)PURR", "True\n");
}

TEST(StringCompileRun, Isupper) {
    e2e::expect_e2e("string_isupper", R"PURR(import io
import string
io.print(string.isupper("MEOW"))
)PURR", "True\n");
}

TEST(StringCompileRun, Islower) {
    e2e::expect_e2e("string_islower", R"PURR(import io
import string
io.print(string.islower("meow"))
)PURR", "True\n");
}
