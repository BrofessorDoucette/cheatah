#include "os.hpp"

#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace os = cheatah::os;

// The os.path.join concept must accept string-constructible types and reject the
// rest, without narrowing today's accepted set.
static_assert(os::StringLike<const char*>);
static_assert(os::StringLike<std::string>);
static_assert(os::StringLike<std::string_view>);
static_assert(!os::StringLike<int>);

TEST(CheatahOs, PathJoin) {
    EXPECT_EQ(os::path::join("a", "b", "c"), "a/b/c");
    EXPECT_EQ(os::path::join("only"), "only");
}

TEST(CheatahOs, PathSplitext) {
    const auto [root, ext] = os::path::splitext("dir/file.purr");
    EXPECT_EQ(root, "dir/file");
    EXPECT_EQ(ext, ".purr");
    const auto [root2, ext2] = os::path::splitext("noext");
    EXPECT_EQ(root2, "noext");
    EXPECT_EQ(ext2, "");
}

TEST(CheatahOs, PathBasenameDirname) {
    EXPECT_EQ(os::path::basename("a/b/c.txt"), "c.txt");
    EXPECT_EQ(os::path::dirname("a/b/c.txt"), "a/b");
}

TEST(CheatahOs, CwdAndCpuCount) {
    EXPECT_FALSE(os::getcwd().empty());
    EXPECT_GT(os::cpu_count(), 0u);
}

TEST(CheatahOs, GetenvFallback) {
    EXPECT_EQ(os::getenv("CHEATAH_NONEXISTENT_VAR_xyz", "fallback"), "fallback");
}

TEST(CheatahOs, SetenvThenGetenv) {
    os::setenv("CHEATAH_PURR_TEST_VAR", "meow");
    EXPECT_EQ(os::getenv("CHEATAH_PURR_TEST_VAR"), "meow");
}

TEST(CheatahOs, MakeDirExistsThenRemove) {
    const std::string dir = "purr_os_tmpdir";
    os::remove(dir);  // start clean
    os::mkdir(dir);
    EXPECT_TRUE(os::path::exists(dir));
    EXPECT_TRUE(os::path::isdir(dir));
    os::rmdir(dir);
    EXPECT_FALSE(os::path::exists(dir));
}

TEST(CheatahOs, FileQueriesIsfileAndGetsize) {
    const std::string f = "purr_os_file.txt";
    { std::ofstream o(f); o << "12345"; }
    EXPECT_TRUE(os::path::exists(f));
    EXPECT_TRUE(os::path::isfile(f));
    EXPECT_FALSE(os::path::isdir(f));
    EXPECT_EQ(os::path::getsize(f), 5u);
    EXPECT_TRUE(os::remove(f));
    EXPECT_FALSE(os::path::exists(f));
}

TEST(CheatahOs, AbspathAndNormpath) {
    EXPECT_EQ(os::path::normpath("a/./b/../c"), "a/c");
    const std::string ap = os::path::abspath("x");
    ASSERT_FALSE(ap.empty());
    EXPECT_EQ(ap.front(), '/');  // absolute path
}

TEST(CheatahOs, MakedirsAndChdir) {
    const std::string nested = "purr_os_a/b/c";
    os::makedirs(nested);
    EXPECT_TRUE(os::path::isdir(nested));
    const std::string cwd = os::getcwd();
    os::chdir("purr_os_a");
    EXPECT_NE(os::getcwd(), cwd);
    os::chdir(cwd);  // restore
    EXPECT_EQ(os::getcwd(), cwd);
    os::rmdir("purr_os_a/b/c");
    os::rmdir("purr_os_a/b");
    os::rmdir("purr_os_a");
    EXPECT_FALSE(os::path::exists("purr_os_a"));
}

TEST(CheatahOs, ListdirAndRename) {
    const std::string dir = "purr_os_list";
    os::remove(dir);
    os::mkdir(dir);
    { std::ofstream o(dir + "/one.txt"); o << "x"; }
    const std::vector<std::string> entries = os::listdir(dir);
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0], "one.txt");  // listdir returns basenames
    os::rename(dir + "/one.txt", dir + "/two.txt");
    EXPECT_TRUE(os::path::isfile(dir + "/two.txt"));
    EXPECT_FALSE(os::path::isfile(dir + "/one.txt"));
    os::remove(dir + "/two.txt");
    os::rmdir(dir);
}

TEST(CheatahOs, PidAndSystem) {
    EXPECT_GT(os::getpid(), 0);
    EXPECT_EQ(os::system("true"), 0);
    EXPECT_NE(os::system("false"), 0);
}
