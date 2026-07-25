// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "sys.hpp"

#include <array>
#include <string>
#include <vector>

#include <gtest/gtest.h>

namespace sys = cheatah::sys;

// The stable extern "C" hook the cheatah runtime resolves with dlsym and calls to
// forward the program's arguments (defined in sys.cpp; not declared in sys.hpp
// because it is never cheatah-visible).
extern "C" void cheatah_set_argv(int argc, char** argv);

namespace {

// Build a mutable argv-style vector from string literals (C main() hands the
// program non-const char*, so the test feeds set_argv the same shape).
std::vector<char*> make_argv(std::initializer_list<const char*> args) {
    std::vector<char*> v;
    v.reserve(args.size());
    for (const char* a : args) v.push_back(const_cast<char*>(a));
    return v;
}

}  // namespace

TEST(CheatahSys, Argv) {
    auto raw = make_argv({"prog", "alpha", "beta gamma"});
    sys::set_argv(static_cast<int>(raw.size()), raw.data());

    ASSERT_EQ(sys::argv.size(), 3u);
    EXPECT_EQ(sys::argv[0], "prog");
    EXPECT_EQ(sys::argv[1], "alpha");
    EXPECT_EQ(sys::argv[2], "beta gamma");  // one argument, embedded space preserved
}

TEST(CheatahSys, ArgvCopiesRatherThanAliases) {
    // set_argv must COPY the C strings: the runtime's argv storage is not owned by
    // the module, so sys.argv must stay valid after the originals change.
    std::array<char, 8> mutable_arg = {'f', 'i', 'r', 's', 't', '\0'};
    std::array<char*, 1> raw = {mutable_arg.data()};
    sys::set_argv(1, raw.data());
    mutable_arg[0] = 'w';  // clobber the source buffer

    ASSERT_EQ(sys::argv.size(), 1u);
    EXPECT_EQ(sys::argv[0], "first");
}

TEST(CheatahSys, SetArgvReplacesPreviousArguments) {
    auto first = make_argv({"prog", "one", "two"});
    sys::set_argv(static_cast<int>(first.size()), first.data());
    ASSERT_EQ(sys::argv.size(), 3u);

    auto second = make_argv({"other"});
    sys::set_argv(static_cast<int>(second.size()), second.data());
    ASSERT_EQ(sys::argv.size(), 1u);  // stale entries from the first call are gone
    EXPECT_EQ(sys::argv[0], "other");
}

TEST(CheatahSys, SetArgvZeroCountYieldsEmptyArgv) {
    auto raw = make_argv({"prog"});
    sys::set_argv(static_cast<int>(raw.size()), raw.data());
    ASSERT_FALSE(sys::argv.empty());

    sys::set_argv(0, raw.data());
    EXPECT_TRUE(sys::argv.empty());
}

TEST(CheatahSys, SetArgvNegativeCountYieldsEmptyArgv) {
    auto raw = make_argv({"prog", "arg"});
    sys::set_argv(static_cast<int>(raw.size()), raw.data());
    ASSERT_FALSE(sys::argv.empty());

    sys::set_argv(-1, raw.data());  // hostile count: clears, never reads argv_
    EXPECT_TRUE(sys::argv.empty());
}

TEST(CheatahSys, SetArgvNullVectorYieldsEmptyArgv) {
    auto raw = make_argv({"prog"});
    sys::set_argv(static_cast<int>(raw.size()), raw.data());
    ASSERT_FALSE(sys::argv.empty());

    sys::set_argv(2, nullptr);  // claimed count with no storage: fail-safe to empty
    EXPECT_TRUE(sys::argv.empty());
}

TEST(CheatahSys, SetArgvNullEntryBecomesEmptyString) {
    auto raw = make_argv({"prog", nullptr, "after"});
    sys::set_argv(static_cast<int>(raw.size()), raw.data());

    ASSERT_EQ(sys::argv.size(), 3u);  // the hole is kept, not dropped, so indexes hold
    EXPECT_EQ(sys::argv[0], "prog");
    EXPECT_EQ(sys::argv[1], "");
    EXPECT_EQ(sys::argv[2], "after");
}

TEST(CheatahSys, ExportedRuntimeHookForwardsToSetArgv) {
    auto raw = make_argv({"module.so", "user-arg"});
    cheatah_set_argv(static_cast<int>(raw.size()), raw.data());

    ASSERT_EQ(sys::argv.size(), 2u);
    EXPECT_EQ(sys::argv[0], "module.so");
    EXPECT_EQ(sys::argv[1], "user-arg");
}
