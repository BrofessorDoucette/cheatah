#include "os.hpp"

#include <string>
#include <string_view>
#include <utility>

#include <gtest/gtest.h>

namespace os = cheatah::purrscript::os;

// The os.path.join concept must accept string-constructible types and reject the
// rest, without narrowing today's accepted set.
static_assert(os::StringLike<const char*>);
static_assert(os::StringLike<std::string>);
static_assert(os::StringLike<std::string_view>);
static_assert(!os::StringLike<int>);

TEST(PurrscriptOs, PathJoin) {
    EXPECT_EQ(os::path::join("a", "b", "c"), "a/b/c");
    EXPECT_EQ(os::path::join("only"), "only");
}

TEST(PurrscriptOs, PathSplitext) {
    const auto [root, ext] = os::path::splitext("dir/file.purr");
    EXPECT_EQ(root, "dir/file");
    EXPECT_EQ(ext, ".purr");
    const auto [root2, ext2] = os::path::splitext("noext");
    EXPECT_EQ(root2, "noext");
    EXPECT_EQ(ext2, "");
}

TEST(PurrscriptOs, PathBasenameDirname) {
    EXPECT_EQ(os::path::basename("a/b/c.txt"), "c.txt");
    EXPECT_EQ(os::path::dirname("a/b/c.txt"), "a/b");
}

TEST(PurrscriptOs, CwdAndCpuCount) {
    EXPECT_FALSE(os::getcwd().empty());
    EXPECT_GT(os::cpu_count(), 0u);
}

TEST(PurrscriptOs, GetenvFallback) {
    EXPECT_EQ(os::getenv("CHEATAH_NONEXISTENT_VAR_xyz", "fallback"), "fallback");
}

TEST(PurrscriptOs, SetenvThenGetenv) {
    os::setenv("CHEATAH_PURR_TEST_VAR", "meow");
    EXPECT_EQ(os::getenv("CHEATAH_PURR_TEST_VAR"), "meow");
}

TEST(PurrscriptOs, MakeDirExistsThenRemove) {
    const std::string dir = "purr_os_tmpdir";
    os::remove(dir);  // start clean
    os::mkdir(dir);
    EXPECT_TRUE(os::path::exists(dir));
    EXPECT_TRUE(os::path::isdir(dir));
    os::rmdir(dir);
    EXPECT_FALSE(os::path::exists(dir));
}
