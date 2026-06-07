#include "io.hpp"

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace io = cheatah::io;

// The io concept must accept exactly what the templates already take (streamable
// types) and reject the rest, so a bad call gives a clear diagnostic.
static_assert(io::Streamable<int>);
static_assert(io::Streamable<std::string>);
static_assert(io::Streamable<const char*>);
static_assert(!io::Streamable<std::vector<int>>);

TEST(CheatahIo, PrintWritesSpaceSeparatedLine) {
    std::ostringstream cap;
    std::streambuf* old = std::cout.rdbuf(cap.rdbuf());
    io::print("meow", 42, "purr");
    std::cout.rdbuf(old);
    EXPECT_EQ(cap.str(), "meow 42 purr\n");
}

TEST(CheatahIo, PrintNoArgsIsJustNewline) {
    std::ostringstream cap;
    std::streambuf* old = std::cout.rdbuf(cap.rdbuf());
    io::print();
    std::cout.rdbuf(old);
    EXPECT_EQ(cap.str(), "\n");
}

TEST(CheatahIo, StrFormatsPythonStyle) {
    EXPECT_EQ(io::str(42), "42");
    EXPECT_EQ(io::str(true), "True");
    EXPECT_EQ(io::str(false), "False");
    EXPECT_EQ(io::str(std::string("meow")), "meow");
}

TEST(CheatahIo, ReprQuotesStrings) {
    EXPECT_EQ(io::repr(std::string("meow")), "'meow'");
    EXPECT_EQ(io::repr("purr"), "'purr'");
    EXPECT_EQ(io::repr(42), "42");
}

TEST(CheatahIo, FormatSubstitutesBraces) {
    EXPECT_EQ(io::format("{} ate {} fish", "cat", 3), "cat ate 3 fish");
    EXPECT_EQ(io::format("no slots here", 1, 2), "no slots here");
}

TEST(CheatahIo, FileWriteThenReadWhole) {
    const std::string path = "purr_io_whole_tmp.txt";
    {
        io::File f = io::open(path, "w");
        f.write("meow\npurr\n");
    }
    {
        io::File f = io::open(path, "r");
        EXPECT_EQ(f.read(), "meow\npurr\n");
    }
    std::filesystem::remove(path);
}

TEST(CheatahIo, FileReadlineThenReadlines) {
    const std::string path = "purr_io_lines_tmp.txt";
    {
        io::File f = io::open(path, "w");
        f.write("meow\npurr\nnap\n");
    }
    {
        io::File f = io::open(path, "r");
        EXPECT_EQ(f.readline(), "meow");
        const std::vector<std::string> rest = f.readlines();
        ASSERT_EQ(rest.size(), 2u);
        EXPECT_EQ(rest[0], "purr");
        EXPECT_EQ(rest[1], "nap");
    }
    std::filesystem::remove(path);
}

TEST(CheatahIo, FileAppendMode) {
    const std::string path = "purr_io_append_tmp.txt";
    {
        io::File f = io::open(path, "w");
        f.write("meow\n");
    }
    {
        io::File f = io::open(path, "a");
        f.write("purr\n");
    }
    {
        io::File f = io::open(path, "r");
        EXPECT_EQ(f.read(), "meow\npurr\n");
    }
    std::filesystem::remove(path);
}

TEST(CheatahIo, FileIsOpenAndClose) {
    const std::string path = "purr_io_isopen_tmp.txt";
    io::File f = io::open(path, "w");
    EXPECT_TRUE(f.is_open());
    f.write("x");
    f.close();
    EXPECT_FALSE(f.is_open());
    std::filesystem::remove(path);
}

TEST(CheatahIo, InputReadsALine) {
    std::istringstream fake("hello world\nsecond\n");
    std::streambuf* saved = std::cin.rdbuf(fake.rdbuf());  // feed stdin
    EXPECT_EQ(io::input("prompt> "), "hello world");
    EXPECT_EQ(io::input(), "second");
    std::cin.rdbuf(saved);  // restore
}

TEST(CheatahIo, FormatMultiArgAndExtraArgs) {
    EXPECT_EQ(io::format("{}-{}", 1, 2), "1-2");  // recursion across placeholders
    EXPECT_EQ(io::format("{}", 1, 2, 3), "1");    // more args than placeholders → dropped
    EXPECT_EQ(io::format("none", 7), "none");     // no placeholders at all
}
