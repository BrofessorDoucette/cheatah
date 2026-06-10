// Compile-run unit tests for the `os` module: one test per function. Each writes
// a tiny .purr that calls a single os (or os.path) function, compiles it with
// purrc, runs it under the cheatah runtime, and asserts the exact stdout.
// Complements the in-process unit tests (stdlib/tests/os_test.cpp) and the
// per-module system-level test (StdlibE2E.Os).
//
// Programs must be DETERMINISTIC. Purely-lexical os.path helpers print their
// result directly. Filesystem functions operate under a UNIQUE /tmp directory
// created inside the .purr and print a deterministic property (e.g. mkdir then
// os.path.isdir -> True). Non-deterministic functions (getpid, getcwd,
// cpu_count, system exit status, getenv of a process-set var) assert a
// deterministic property instead of the raw value.
//
// SKIPPED: none — every os/os.path function below is covered. The only thing
// that cannot be printed directly is os.path.splitext (returns a std::pair, not
// io.print-Streamable); it is covered via the pair's `.first`/`.second` members.
#include "e2e_harness.hpp"

// ---- os.path: purely-lexical helpers (deterministic from the literal) -------

TEST(OsCompileRun, PathJoin) {
    e2e::expect_e2e("os_path_join", R"PURR(import io
import os
io.print(os.path.join("a", "b", "c"))
)PURR", "a/b/c\n");
}

TEST(OsCompileRun, PathBasename) {
    e2e::expect_e2e("os_path_basename", R"PURR(import io
import os
io.print(os.path.basename("/a/b/c.txt"))
)PURR", "c.txt\n");
}

TEST(OsCompileRun, PathDirname) {
    e2e::expect_e2e("os_path_dirname", R"PURR(import io
import os
io.print(os.path.dirname("/a/b/c.txt"))
)PURR", "/a/b\n");
}

TEST(OsCompileRun, PathNormpath) {
    e2e::expect_e2e("os_path_normpath", R"PURR(import io
import os
io.print(os.path.normpath("a/./b/../c"))
)PURR", "a/c\n");
}

TEST(OsCompileRun, PathSplitext) {
    // splitext returns a std::pair (not io.print-Streamable); read its members.
    e2e::expect_e2e("os_path_splitext", R"PURR(import io
import os
io.print(os.path.splitext("dir/file.purr").first, os.path.splitext("dir/file.purr").second)
)PURR", "dir/file .purr\n");
}

TEST(OsCompileRun, PathAbspath) {
    // An already-absolute path is returned unchanged: deterministic.
    e2e::expect_e2e("os_path_abspath", R"PURR(import io
import os
io.print(os.path.abspath("/already/abs"))
)PURR", "/already/abs\n");
}

// ---- os.path: filesystem queries (deterministic against fixed paths) --------

TEST(OsCompileRun, PathExists) {
    e2e::expect_e2e("os_path_exists", R"PURR(import io
import os
io.print(os.path.exists("/tmp"))
)PURR", "True\n");
}

TEST(OsCompileRun, PathIsdir) {
    e2e::expect_e2e("os_path_isdir", R"PURR(import io
import os
io.print(os.path.isdir("/tmp"))
)PURR", "True\n");
}

TEST(OsCompileRun, PathIsfile) {
    // /tmp is a directory, not a regular file.
    e2e::expect_e2e("os_path_isfile", R"PURR(import io
import os
io.print(os.path.isfile("/tmp"))
)PURR", "False\n");
}

TEST(OsCompileRun, PathGetsize) {
    // Create a 5-byte file, then query its size.
    e2e::expect_e2e("os_path_getsize", R"PURR(import io
import os
os.mkdir("/tmp/cr_os_getsize_dir")
os.system("printf 12345 > /tmp/cr_os_getsize_dir/f.txt")
io.print(os.path.getsize("/tmp/cr_os_getsize_dir/f.txt"))
os.remove("/tmp/cr_os_getsize_dir/f.txt")
os.rmdir("/tmp/cr_os_getsize_dir")
)PURR", "5\n");
}

// ---- os: directory creation / removal ---------------------------------------

TEST(OsCompileRun, Mkdir) {
    e2e::expect_e2e("os_mkdir", R"PURR(import io
import os
os.mkdir("/tmp/cr_os_mkdir_dir")
io.print(os.path.isdir("/tmp/cr_os_mkdir_dir"))
os.rmdir("/tmp/cr_os_mkdir_dir")
)PURR", "True\n");
}

TEST(OsCompileRun, Makedirs) {
    e2e::expect_e2e("os_makedirs", R"PURR(import io
import os
os.makedirs("/tmp/cr_os_makedirs_dir/a/b/c")
io.print(os.path.isdir("/tmp/cr_os_makedirs_dir/a/b/c"))
os.rmdir("/tmp/cr_os_makedirs_dir/a/b/c")
os.rmdir("/tmp/cr_os_makedirs_dir/a/b")
os.rmdir("/tmp/cr_os_makedirs_dir/a")
os.rmdir("/tmp/cr_os_makedirs_dir")
)PURR", "True\n");
}

TEST(OsCompileRun, Rmdir) {
    e2e::expect_e2e("os_rmdir", R"PURR(import io
import os
os.mkdir("/tmp/cr_os_rmdir_dir")
os.rmdir("/tmp/cr_os_rmdir_dir")
io.print(os.path.exists("/tmp/cr_os_rmdir_dir"))
)PURR", "False\n");
}

// ---- os: file operations ----------------------------------------------------

TEST(OsCompileRun, Remove) {
    // remove() returns true when something was deleted.
    e2e::expect_e2e("os_remove", R"PURR(import io
import os
os.mkdir("/tmp/cr_os_remove_dir")
os.system("printf x > /tmp/cr_os_remove_dir/f.txt")
io.print(os.remove("/tmp/cr_os_remove_dir/f.txt"))
os.rmdir("/tmp/cr_os_remove_dir")
)PURR", "True\n");
}

TEST(OsCompileRun, Rename) {
    e2e::expect_e2e("os_rename", R"PURR(import io
import os
os.mkdir("/tmp/cr_os_rename_dir")
os.system("printf x > /tmp/cr_os_rename_dir/one.txt")
os.rename("/tmp/cr_os_rename_dir/one.txt", "/tmp/cr_os_rename_dir/two.txt")
io.print(os.path.isfile("/tmp/cr_os_rename_dir/two.txt"))
os.remove("/tmp/cr_os_rename_dir/two.txt")
os.rmdir("/tmp/cr_os_rename_dir")
)PURR", "True\n");
}

TEST(OsCompileRun, Listdir) {
    // A directory with a single known entry: listdir returns basenames.
    e2e::expect_e2e("os_listdir", R"PURR(import io
import os
os.mkdir("/tmp/cr_os_listdir_dir")
os.system("printf x > /tmp/cr_os_listdir_dir/only.txt")
io.print(os.listdir("/tmp/cr_os_listdir_dir")[0])
os.remove("/tmp/cr_os_listdir_dir/only.txt")
os.rmdir("/tmp/cr_os_listdir_dir")
)PURR", "only.txt\n");
}

// ---- os: working directory --------------------------------------------------

TEST(OsCompileRun, Getcwd) {
    // The cwd is non-deterministic; assert it is a non-empty path.
    e2e::expect_e2e("os_getcwd", R"PURR(import io
import os
io.print(len(os.getcwd()) > 0)
)PURR", "True\n");
}

TEST(OsCompileRun, Chdir) {
    // chdir into a unique temp dir, then confirm getcwd's basename matches.
    e2e::expect_e2e("os_chdir", R"PURR(import io
import os
os.makedirs("/tmp/cr_os_chdir_dir")
os.chdir("/tmp/cr_os_chdir_dir")
io.print(os.path.basename(os.getcwd()))
os.chdir("/tmp")
os.rmdir("/tmp/cr_os_chdir_dir")
)PURR", "cr_os_chdir_dir\n");
}

// ---- os: environment --------------------------------------------------------

TEST(OsCompileRun, Setenv) {
    // setenv then getenv round-trips within the process.
    e2e::expect_e2e("os_setenv", R"PURR(import io
import os
os.setenv("CHEATAH_CR_OS_VAR", "meow")
io.print(os.getenv("CHEATAH_CR_OS_VAR"))
)PURR", "meow\n");
}

TEST(OsCompileRun, Getenv) {
    // An unset variable returns the supplied fallback.
    e2e::expect_e2e("os_getenv", R"PURR(import io
import os
io.print(os.getenv("CHEATAH_CR_OS_NONEXISTENT_zzz", "fallback"))
)PURR", "fallback\n");
}

// ---- os: process / system ---------------------------------------------------

TEST(OsCompileRun, Getpid) {
    // The pid is non-deterministic; assert it is positive.
    e2e::expect_e2e("os_getpid", R"PURR(import io
import os
io.print(os.getpid() > 0)
)PURR", "True\n");
}

TEST(OsCompileRun, CpuCount) {
    // The exact count is host-dependent; assert at least one CPU.
    e2e::expect_e2e("os_cpu_count", R"PURR(import io
import os
io.print(os.cpu_count() >= 1)
)PURR", "True\n");
}

TEST(OsCompileRun, System) {
    // `true` exits 0; the exact value of `false` is shell-dependent, so assert
    // only that it is non-zero.
    e2e::expect_e2e("os_system", R"PURR(import io
import os
io.print(os.system("true"))
io.print(os.system("false") != 0)
)PURR", "0\nTrue\n");
}

TEST(OsCompileRun, Urandom) {
    // urandom returns real entropy (non-deterministic), so assert its PROPERTIES: the
    // requested length, and that two independent draws don't match.
    e2e::expect_e2e("os_urandom", R"PURR(import io
import os
io.print(len(os.urandom(32)))
io.print(os.urandom(16) != os.urandom(16))
)PURR", "32\nTrue\n");
}
