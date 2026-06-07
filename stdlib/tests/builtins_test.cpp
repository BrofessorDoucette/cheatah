#include "builtins.hpp"

#include <string>

#include <gtest/gtest.h>

namespace b = cheatah::builtins;

TEST(CheatahBuiltins, LenOrdChr) {
    EXPECT_EQ(b::len("meow"), 4u);
    EXPECT_EQ(b::ord("A"), 65);
    EXPECT_EQ(b::chr(65), "A");
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
