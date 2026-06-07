#include "string.hpp"

#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace str = cheatah::string;

TEST(CheatahString, Case) {
    EXPECT_EQ(str::upper("meow"), "MEOW");
    EXPECT_EQ(str::lower("MeOw"), "meow");
    EXPECT_EQ(str::capitalize("hello world"), "Hello world");
    EXPECT_EQ(str::title("hello world"), "Hello World");
    EXPECT_EQ(str::swapcase("Meow"), "mEOW");
}

TEST(CheatahString, Trimming) {
    EXPECT_EQ(str::strip("  meow \t"), "meow");
    EXPECT_EQ(str::lstrip("xxmeow", "x"), "meow");
    EXPECT_EQ(str::rstrip("meowyy", "y"), "meow");
}

TEST(CheatahString, SearchAndTest) {
    EXPECT_TRUE(str::startswith("meow", "me"));
    EXPECT_TRUE(str::endswith("meow", "ow"));
    EXPECT_TRUE(str::contains("meow", "eo"));
    EXPECT_EQ(str::find("meow meow", "meow"), 0);
    EXPECT_EQ(str::rfind("meow meow", "meow"), 5);
    EXPECT_EQ(str::find("meow", "z"), -1);
    EXPECT_EQ(str::count("meow meow meow", "meow"), 3);
}

TEST(CheatahString, Transform) {
    EXPECT_EQ(str::replace("meow meow", "e", "3"), "m3ow m3ow");
    EXPECT_EQ(str::split("a,b,c", ","), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(str::split("  a   b "), (std::vector<std::string>{"a", "b"}));
    EXPECT_EQ(str::splitlines("a\nb\r\nc"), (std::vector<std::string>{"a", "b", "c"}));
    EXPECT_EQ(str::capwords("the quick brown"), "The Quick Brown");
    EXPECT_EQ(str::join("-", std::vector<std::string>{"a", "b", "c"}), "a-b-c");
    EXPECT_EQ(str::join(", ", std::vector<const char*>{"x", "y"}), "x, y");  // any string-like elem
}

TEST(CheatahString, Padding) {
    EXPECT_EQ(str::ljust("cat", 5), "cat  ");
    EXPECT_EQ(str::rjust("cat", 5), "  cat");
    EXPECT_EQ(str::center("cat", 9, "*"), "***cat***");
    EXPECT_EQ(str::zfill("42", 5), "00042");
    EXPECT_EQ(str::zfill("-42", 5), "-0042");
}

TEST(CheatahString, Classification) {
    EXPECT_TRUE(str::isdigit("123"));
    EXPECT_FALSE(str::isdigit("12a"));
    EXPECT_FALSE(str::isdigit(""));
    EXPECT_TRUE(str::isalpha("abc"));
    EXPECT_TRUE(str::isupper("MEOW"));
    EXPECT_TRUE(str::islower("meow"));
    EXPECT_FALSE(str::isupper("Meow"));
}

TEST(CheatahString, ClassificationAlnumAndSpace) {
    EXPECT_TRUE(str::isalnum("abc123"));
    EXPECT_FALSE(str::isalnum("abc 123"));
    EXPECT_FALSE(str::isalnum(""));  // empty → false
    EXPECT_TRUE(str::isspace("  \t\n"));
    EXPECT_FALSE(str::isspace("a b"));
    EXPECT_FALSE(str::isspace(""));
}
