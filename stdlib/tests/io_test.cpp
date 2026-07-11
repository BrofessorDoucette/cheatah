// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "io.hpp"

#include <complex>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <gtest/gtest.h>

namespace io = cheatah::io;

TEST(CheatahIo, StrRendersComplex) {
    using C = std::complex<double>;
    EXPECT_EQ(io::str(C(8, 3)), "8+3j");    // positive imaginary
    EXPECT_EQ(io::str(C(1, -2)), "1-2j");   // negative imaginary
    EXPECT_EQ(io::str(C(1, 0)), "1+0j");    // +0 (not "-0") after a conjugate
    EXPECT_EQ(io::str(std::conj(C(1, 0))), "1+0j");
    // repr matches str (numbers are not quoted), and a list of complex prints readably.
    EXPECT_EQ(io::repr(C(2, -5)), "2-5j");
    EXPECT_EQ(io::str(std::vector<C>{C(0, 1), C(2, 0)}), "[0+1j, 2+0j]");
}

TEST(CheatahIo, StrByteWidthIntsAreNumbers) {
    // i8/u8 (signed char / unsigned char) print as NUMBERS, not characters — mirrors
    // builtins::str, so print/repr of a narrow-width value shows digits. See io.hpp str(signed char).
    EXPECT_EQ(io::str(static_cast<signed char>(65)), "65");
    EXPECT_EQ(io::str(static_cast<signed char>(-5)), "-5");
    EXPECT_EQ(io::str(static_cast<unsigned char>(200)), "200");
    EXPECT_EQ(io::str(static_cast<unsigned char>(0)), "0");
}

// The io concept must accept exactly what the templates already take (streamable
// types) and reject the rest, so a bad call gives a clear diagnostic.
static_assert(io::Streamable<int>);
static_assert(io::Streamable<std::string>);
static_assert(io::Streamable<const char*>);
static_assert(!io::Streamable<std::vector<int>>);

// print() requires Printable, NOT Streamable: a value is printable if it streams
// directly, has a `str()`, or is a list/dict of printable elements.
namespace {
struct WithStr { std::string str() const { return "W"; } };
struct NotPrintable { int z; };  // not streamable, no str()
}  // namespace
static_assert(io::Printable<int>);
static_assert(io::Printable<std::vector<int>>);
static_assert(io::Printable<std::vector<std::vector<std::string>>>);
static_assert(io::Printable<std::unordered_map<std::string, int>>);
static_assert(io::Printable<WithStr>);
static_assert(!io::Printable<NotPrintable>);              // no stream op, no str()
static_assert(!io::Printable<std::vector<NotPrintable>>);  // element not printable

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

namespace {
// A stand-in for a cheatah struct: it both streams (operator<<, the compact form rprint/str
// use) and exposes a cheatah_pretty_print member (the pretty form print uses).
struct PrettyStub {
    friend std::ostream& operator<<(std::ostream& os, const PrettyStub&) { return os << "P(compact)"; }
    void cheatah_pretty_print(std::ostream& os, long long indent) const {
        os << std::string(static_cast<std::size_t>(indent), ' ') << "P(\n    x = 1\n)";
    }
};
}  // namespace

TEST(CheatahIo, PrintPrettyPrintsStructs) {
    std::ostringstream cap;
    std::streambuf* old = std::cout.rdbuf(cap.rdbuf());
    io::print(PrettyStub{});  // has cheatah_pretty_print -> pretty branch
    std::cout.rdbuf(old);
    EXPECT_EQ(cap.str(), "P(\n    x = 1\n)\n");
}

TEST(CheatahIo, RprintIsCompact) {
    std::ostringstream cap;
    std::streambuf* old = std::cout.rdbuf(cap.rdbuf());
    io::rprint(PrettyStub{}, 7);  // compact (operator<<) + a scalar, space-separated
    std::cout.rdbuf(old);
    EXPECT_EQ(cap.str(), "P(compact) 7\n");
}

TEST(CheatahIo, StrRendersContainersAndObjects) {
    EXPECT_EQ(io::str(std::vector<long long>{1, 2, 3}), "[1, 2, 3]");
    EXPECT_EQ(io::str(std::vector<std::string>{"a", "b"}), "['a', 'b']");  // nested strings quoted
    EXPECT_EQ(io::str(std::vector<std::vector<long long>>{{1, 2}, {3, 4}}), "[[1, 2], [3, 4]]");
    std::unordered_map<std::string, long long> m{{"a", 1}};
    EXPECT_EQ(io::str(m), "{'a': 1}");
    EXPECT_EQ(io::str(WithStr{}), "W");                    // a type with a str() method
    EXPECT_EQ(io::str(std::vector<WithStr>{{}, {}}), "[W, W]");
}

TEST(CheatahIo, StrFormatsPythonStyle) {
    EXPECT_EQ(io::str(42), "42");
    EXPECT_EQ(io::str(true), "True");
    EXPECT_EQ(io::str(false), "False");
    EXPECT_EQ(io::str(std::string("meow")), "meow");
}

TEST(CheatahIo, FixedFormatsAndRounds) {
    EXPECT_EQ(io::fixed(3.14159, 2), "3.14");
    EXPECT_EQ(io::fixed(2.675, 2), "2.67");     // binary 2.675 is 2.67499…: correct rounding
    EXPECT_EQ(io::fixed(12.0, 1), "12.0");
    EXPECT_EQ(io::fixed(0.5, 0), "0");          // half-to-even at the integer boundary
    EXPECT_EQ(io::fixed(1.5, 0), "2");
    EXPECT_EQ(io::fixed(-1.25, 1), "-1.2");
    EXPECT_EQ(io::fixed(7.0, -3), "7");         // negative places clamps to 0
    EXPECT_EQ(io::fixed(0.1, 17), "0.10000000000000001");  // places capped at 17
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

TEST(CheatahIo, ReadFileWholeAndBinary) {
    const std::string path = "purr_io_readfile_tmp.bin";
    std::string payload = "meow\npurr\n";
    payload.push_back('\0');          // embedded NUL — read_file must preserve it
    payload += "after-nul";
    {
        io::File f = io::open(path, "wb");
        f.write(payload);
    }
    EXPECT_EQ(io::read_file(path), payload);          // whole file, byte-exact
    EXPECT_EQ(io::read_file("no_such_file_qzx.bin"), "");  // missing → ""
    std::filesystem::remove(path);
}
