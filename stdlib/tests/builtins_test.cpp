// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "builtins.hpp"

#include <stdexcept>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

namespace b = cheatah::builtins;

TEST(CheatahBuiltins, LenOrdChr) {
    EXPECT_EQ(b::len("meow"), 4u);
    EXPECT_EQ(b::ord("A"), 65);
    EXPECT_EQ(b::chr(65), "A");
}

TEST(CheatahBuiltins, Str) {
    // Streamable template: integers, floats (default 6-sig-digit form), and strings.
    EXPECT_EQ(b::str(42LL), "42");
    EXPECT_EQ(b::str(-7LL), "-7");
    EXPECT_EQ(b::str(3.14), "3.14");
    EXPECT_EQ(b::str(std::string("hi")), "hi");
    // bool overload: Python's capitalized spelling, not 1/0.
    EXPECT_EQ(b::str(true), "True");
    EXPECT_EQ(b::str(false), "False");
}

TEST(CheatahBuiltins, StrByteWidthIntsAreNumbers) {
    // i8/u8 (signed char / unsigned char) render as NUMBERS, not characters — the dedicated
    // overloads promote to a wider integer before to_string. Streamed as a raw char, 65 would
    // print 'A'; here it must be "65".
    EXPECT_EQ(b::str(static_cast<signed char>(65)), "65");
    EXPECT_EQ(b::str(static_cast<signed char>(-5)), "-5");
    EXPECT_EQ(b::str(static_cast<unsigned char>(200)), "200");
    EXPECT_EQ(b::str(static_cast<unsigned char>(0)), "0");
}

TEST(CheatahBuiltins, BaseReprs) {
    EXPECT_EQ(b::hex(255), "0xff");
    EXPECT_EQ(b::oct(8), "0o10");
    EXPECT_EQ(b::bin(5), "0b101");
    EXPECT_EQ(b::hex(-255), "-0xff");
    EXPECT_EQ(b::hex(0), "0x0");
}

TEST(CheatahBuiltins, Conversions) {
    EXPECT_EQ(b::to_int("42"), 42);
    EXPECT_EQ(b::to_int(3.9), 3);
    EXPECT_DOUBLE_EQ(b::to_float("2.5"), 2.5);
    EXPECT_TRUE(b::to_bool("x"));
    EXPECT_FALSE(b::to_bool(""));
    EXPECT_FALSE(b::to_bool(0));
    EXPECT_TRUE(b::to_bool(7));
}

TEST(CheatahBuiltins, Ascii) {
    EXPECT_EQ(b::ascii("hi"), "'hi'");
    EXPECT_EQ(b::ascii(std::string("a\tb")), "'a\\x09b'");
}

TEST(CheatahBuiltins, Hash) {
    EXPECT_EQ(b::hash(std::string_view("meow")), b::hash(std::string_view("meow")));
}

TEST(CheatahBuiltins, ToFloatFromInt) {
    EXPECT_DOUBLE_EQ(b::to_float(7LL), 7.0);
    EXPECT_DOUBLE_EQ(b::to_float(-3LL), -3.0);
}

TEST(CheatahBuiltins, ToFloatFromFloat) {
    EXPECT_DOUBLE_EQ(b::to_float(0.95), 0.95);      // identity — must NOT truncate via long long
    EXPECT_DOUBLE_EQ(b::to_float(-0.0169), -0.0169);
}

TEST(CheatahBuiltins, AsciiEscapesQuoteChar) {
    EXPECT_EQ(b::ascii("'"), "'\\''");  // a lone single quote -> \'
}

TEST(CheatahBuiltins, AsciiEscapesBackslashAndQuote) {
    EXPECT_EQ(b::ascii(std::string("a\\b")), "'a\\\\b'");  // backslash → \\
    EXPECT_EQ(b::ascii("it's"), "'it\\'s'");               // single quote → \'
}

TEST(CheatahBuiltins, Append) {
    std::vector<long long> xs;
    b::append(xs, 1);
    b::append(xs, 2LL);
    ASSERT_EQ(xs.size(), 2u);
    EXPECT_EQ(xs[0], 1);
    EXPECT_EQ(xs[1], 2);
}

TEST(CheatahBuiltins, StringPredicates) {
    EXPECT_TRUE(b::startswith("</div>", "</"));
    EXPECT_FALSE(b::startswith("x", "</"));
    EXPECT_TRUE(b::endswith("hello", "lo"));
    EXPECT_FALSE(b::endswith("hi", "lo"));
    EXPECT_TRUE(b::contains("abcd", "bc"));
    EXPECT_FALSE(b::contains("abcd", "zz"));
}

TEST(CheatahBuiltins, IndexString) {
    EXPECT_EQ(b::index(std::string("hello"), 0), "h");
    EXPECT_EQ(b::index(std::string("hello"), -1), "o");  // negative from the end
    EXPECT_THROW(b::index(std::string("hi"), 5), std::out_of_range);
}

TEST(CheatahBuiltins, IndexList) {
    const std::vector<long long> xs{10, 20, 30};
    EXPECT_EQ(b::index(xs, 1), 20);
    EXPECT_EQ(b::index(xs, -1), 30);
    EXPECT_THROW(b::index(xs, 3), std::out_of_range);
}

TEST(CheatahBuiltins, IndexBoolList) {
    // std::vector<bool> is bit-packed (proxy references, no .data()), so it has
    // its own index overload; semantics match every other sequence.
    const std::vector<bool> xs{true, false, true};
    EXPECT_TRUE(b::index(xs, 0));
    EXPECT_FALSE(b::index(xs, 1));
    EXPECT_TRUE(b::index(xs, -1));  // negative from the end
    EXPECT_THROW(b::index(xs, 3), std::out_of_range);
}

TEST(CheatahBuiltins, IndexDict) {
    const std::unordered_map<std::string, long long> m{{"a", 1}, {"b", 2}};
    EXPECT_EQ(b::index(m, std::string("a")), 1);
    // A missing key raises kind "key", NOT the "index" a sequence subscript raises: walking off the end
    // of a list and asking for an entry that was never there are different mistakes, and `except e of
    // "key"` should be able to take one without silently swallowing the other.
    EXPECT_THROW(b::index(m, std::string("z")), b::Error);
    try {
        b::index(m, std::string("z"));
        FAIL() << "expected a raise";
    } catch (const b::Error& e) {
        EXPECT_EQ(e.kind(), b::kErrorKindKey);
        EXPECT_EQ(e.message(), "key not found");
    }
}

TEST(CheatahBuiltins, SliceString) {
    const std::string s = "hello world";
    EXPECT_EQ(b::slice(s, 0, 5), "hello");
    EXPECT_EQ(b::slice(s, 6, b::slice_end), "world");  // s[6:]
    EXPECT_EQ(b::slice(s, 0, b::slice_end), s);          // s[:]
    EXPECT_EQ(b::slice(s, -5, b::slice_end), "world");   // negative start
    EXPECT_EQ(b::slice(s, 3, 1), "");                    // empty when lo >= hi
    EXPECT_EQ(b::slice(s, 0, 100), s);                   // hi clamped to len
}

TEST(CheatahBuiltins, SliceList) {
    const std::vector<long long> xs{1, 2, 3, 4, 5};
    EXPECT_EQ(b::slice(xs, 1, 4), (std::vector<long long>{2, 3, 4}));
    EXPECT_EQ(b::slice(xs, -2, b::slice_end), (std::vector<long long>{4, 5}));
    EXPECT_TRUE(b::slice(xs, 3, 1).empty());
}

TEST(CheatahBuiltins, Division) {
    // truediv (the `/` operator) is ALWAYS floating-point, like Python 3.
    EXPECT_DOUBLE_EQ(b::truediv(6, 4), 1.5);        // int / int -> float
    EXPECT_DOUBLE_EQ(b::truediv(6, 2), 3.0);        // exact, but still a double
    EXPECT_DOUBLE_EQ(b::truediv(7.0, 2.0), 3.5);
    // floordiv (the `//` operator) floors toward -inf, the way Python does.
    EXPECT_EQ(b::floordiv(7, 2), 3);                // a%b != 0, same sign -> no adjust
    EXPECT_EQ(b::floordiv(-7, 2), -4);              // different signs -> floor adjust
    EXPECT_EQ(b::floordiv(6, 2), 3);                // exact (a%b == 0) -> no adjust
    EXPECT_DOUBLE_EQ(b::floordiv(7.0, 2.0), 3.0);   // floating operands -> floored double
    EXPECT_DOUBLE_EQ(b::floordiv(7.0, 2), 3.0);     // mixed -> floored double
}

// Integer `%` takes the sign of the DIVISOR (Python floor-mod), not of the dividend the way C++ does:
// -7 % 2 is 1 here, not -1. Only the by-zero throw was covered before, so the sign-correction itself
// went untested; these pin the branch both ways plus the exact-division and same-sign no-adjust paths.
TEST(CheatahBuiltins, Mod) {
    EXPECT_EQ(b::mod(7, 2), 1);    // same sign -> no adjust
    EXPECT_EQ(b::mod(-7, 2), 1);   // dividend negative, divisor positive -> +b correction
    EXPECT_EQ(b::mod(7, -2), -1);  // dividend positive, divisor negative -> +b correction
    EXPECT_EQ(b::mod(-7, -2), -1); // both negative -> signs already agree, no adjust
    EXPECT_EQ(b::mod(6, 3), 0);    // exact -> r == 0, no adjust
    EXPECT_EQ(b::mod(-6, 3), 0);   // exact and negative -> still 0, must NOT become +3
}

// Integer `//` and `%` by zero raise a CONTROLLED error (std::domain_error) instead of undefined
// behavior — C++ integer divide/modulo by zero is UB (SIGFPE). Float `/`,`//`,`%` are IEEE-safe.
TEST(CheatahBuiltins, IntegerDivideAndModuloByZeroThrow) {
    EXPECT_THROW(b::floordiv(1, 0), std::domain_error);
    EXPECT_THROW(b::floordiv(-5, 0), std::domain_error);
    EXPECT_THROW(b::mod(1, 0), std::domain_error);
    EXPECT_THROW(b::mod(-5, 0), std::domain_error);
}

// ord(): the code point of a one-byte char, unsigned (high bytes are 128..255, never negative).
TEST(CheatahBuiltins, Ord) {
    EXPECT_EQ(b::ord('A'), 65);
    EXPECT_EQ(b::ord('0'), 48);
    EXPECT_EQ(b::ord('\xff'), 255);   // 0xFF -> 255, not -1
}

// ---- errors: the kind/message value type behind `raise` and `except` -----------------------------

TEST(CheatahBuiltins, ErrorCarriesKindAndMessage) {
    const b::Error plain("boom");
    EXPECT_EQ(plain.kind(), b::kErrorKindError) << "an unclassified raise gets the generic kind";
    EXPECT_EQ(plain.message(), "boom");
    EXPECT_STREQ(plain.what(), "boom") << "and it is still a std::exception carrying the message";

    const b::Error classified("io", "disk full");
    EXPECT_EQ(classified.kind(), "io");
    EXPECT_EQ(classified.message(), "disk full");
}

TEST(CheatahBuiltins, ErrorComparesAndPrintsAsItsMessage) {
    const b::Error e("io", "disk full");
    // All four orderings, both string and literal — this is what keeps `except e { if e == "..." }`
    // reading the way it did when a handler bound a bare string.
    EXPECT_TRUE(e == std::string("disk full"));
    EXPECT_TRUE(std::string("disk full") == e);
    EXPECT_TRUE(e == "disk full");
    EXPECT_TRUE("disk full" == e);
    EXPECT_FALSE(e == "io") << "comparison is against the MESSAGE, never the kind";

    std::ostringstream os;
    os << e;
    EXPECT_EQ(os.str(), "disk full") << "streaming yields the sentence, not the kind";
    EXPECT_EQ(b::str(e), "disk full");
}

TEST(CheatahBuiltins, CurrentErrorNormalizesEveryThrownType) {
    // The point of current_error: ONE handler shape covers everything that can arrive, including a
    // type nothing knows about — which previously travelled past every handler and killed the process.
    const auto caught = [](auto&& thrower) {
        try {
            thrower();
        } catch (...) {
            return b::current_error();
        }
        return b::Error("never", "never");
    };

    EXPECT_EQ(caught([] { throw b::Error("io", "passed through"); }).kind(), "io");
    EXPECT_EQ(caught([] { throw std::out_of_range("oops"); }).kind(), b::kErrorKindIndex);
    EXPECT_EQ(caught([] { throw std::domain_error("oops"); }).kind(), b::kErrorKindArithmetic);
    EXPECT_EQ(caught([] { throw std::runtime_error("oops"); }).kind(), b::kErrorKindError);
    EXPECT_EQ(caught([] { throw 42; }).kind(), b::kErrorKindUnknown) << "an int throw is still catchable";
    EXPECT_EQ(caught([] { throw std::out_of_range("keep me"); }).message(), "keep me");
}

TEST(CheatahBuiltins, FinallyRunsOnEveryExitPath) {
    // A guard, not a duplicated block — so it survives the paths a duplicated block would skip.
    int ran = 0;

    {
        auto g = b::make_finally([&] { ++ran; });
    }
    EXPECT_EQ(ran, 1) << "normal fall-through";

    ran = 0;
    const auto with_return = [&]() -> int {
        auto g = b::make_finally([&] { ++ran; });
        return 7;   // the case a duplicated finally body would miss
    };
    EXPECT_EQ(with_return(), 7);
    EXPECT_EQ(ran, 1) << "early return";

    ran = 0;
    bool caught = false;
    try {
        const auto g = b::make_finally([&] { ++ran; });
        static_cast<void>(g);  // held only for its scope-exit effect
        throw b::Error("x", "unwind");
    } catch (const b::Error&) {
        caught = true;  // the throw exists only to unwind through g's scope
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(ran, 1) << "an exception unwinding through the scope";
}

TEST(CheatahBuiltins, FinallySwallowsItsOwnThrowDuringUnwinding) {
    // A finally that throws WHILE an exception is unwinding would terminate the process. Losing the
    // second error is the lesser harm, and this pins that choice so nobody "fixes" it into a crash.
    EXPECT_NO_THROW({
        try {
            auto g = b::make_finally([] { throw std::runtime_error("from the guard"); });
            throw b::Error("first", "the original");
        } catch (const b::Error& e) {
            EXPECT_EQ(e.message(), "the original") << "the original error is what survives";
        }
    });
}
