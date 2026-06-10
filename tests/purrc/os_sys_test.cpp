// System-level test for the cheatah `os` standard-library module.
//
// Unlike os_cr_test.cpp (one tiny program per function), this is a SINGLE
// cohesive filesystem workflow that exercises EVERY public function in
// stdlib/os/os.hpp in one program: it builds a nested directory tree, creates
// and renames and removes files (writing content via os.system), queries them
// (exists/isfile/isdir/getsize/listdir), changes directory, applies the
// purely-lexical os.path helpers, round-trips an environment variable, and
// reports the process/system facts. Everything happens under a unique /tmp
// workspace that is torn down at the end.
//
// Output is DETERMINISTIC: filesystem state is fully controlled, and the
// non-deterministic facts (getpid, getcwd, cpu_count, the exit status of
// `false`) are asserted as PROPERTIES (pid > 0, cpu_count >= 1, cwd basename,
// false != 0) rather than printed raw.
//
// Coverage (all 24 public functions):
//   os.*       getcwd chdir listdir mkdir makedirs rmdir remove rename
//              getenv setenv cpu_count system getpid urandom
//   os.path.*  join exists isfile isdir basename dirname abspath getsize
//              splitext normpath
//
// splitext returns a std::pair (not io.print-Streamable); it is read via the
// pair's .first / .second members, matching os_cr_test.cpp.
#include "e2e_harness.hpp"

TEST(StdlibE2E, Os) {
    e2e::expect_e2e("os_sys", R"PURR(import io
import os

# --- Setup: a unique workspace under /tmp -----------------------------------
let root = "/tmp/cheatah_os_sys_ws"
os.system("rm -rf /tmp/cheatah_os_sys_ws")
os.mkdir(root)

# Remember where we started so we can restore it.
let start = os.getcwd()

# --- makedirs: create a nested tree -----------------------------------------
let nested = os.path.join(root, "a", "b", "c")
os.makedirs(nested)
io.print("isdir nested:", os.path.isdir(nested))

# --- mkdir: create a sibling leaf directory ---------------------------------
let data = os.path.join(root, "data")
os.mkdir(data)
io.print("isdir data:", os.path.isdir(data))

# --- create files via os.system, then query them ----------------------------
let f1 = os.path.join(data, "hello.txt")
os.system("printf 'hello' > " + f1)
io.print("exists f1:", os.path.exists(f1))
io.print("isfile f1:", os.path.isfile(f1))
io.print("isdir  f1:", os.path.isdir(f1))
io.print("getsize f1:", os.path.getsize(f1))

# --- rename a file ----------------------------------------------------------
let f2 = os.path.join(data, "renamed.txt")
os.rename(f1, f2)
io.print("isfile f2:", os.path.isfile(f2))
io.print("isfile f1 gone:", os.path.isfile(f1))

# --- listdir -----------------------------------------------------------------
let entries = os.listdir(data)
io.print("listdir len:", len(entries))
io.print("listdir[0]:", entries[0])

# --- chdir + getcwd: cd into data, confirm basename -------------------------
os.chdir(data)
io.print("cwd basename:", os.path.basename(os.getcwd()))
os.chdir(start)

# --- lexical path helpers ----------------------------------------------------
io.print("join:", os.path.join("x", "y", "z.txt"))
io.print("basename:", os.path.basename("/p/q/r.dat"))
io.print("dirname:", os.path.dirname("/p/q/r.dat"))
io.print("normpath:", os.path.normpath("/p/./q/../r"))
io.print("abspath:", os.path.abspath("/already/abs"))
let se = os.path.splitext("archive.tar.gz")
io.print("splitext:", se.first, se.second)

# --- environment round-trip --------------------------------------------------
os.setenv("CHEATAH_OS_SYS_VAR", "purr")
io.print("getenv set:", os.getenv("CHEATAH_OS_SYS_VAR"))
io.print("getenv fallback:", os.getenv("CHEATAH_OS_SYS_MISSING_zzz", "none"))

# --- process / system facts (asserted as properties) ------------------------
io.print("pid positive:", os.getpid() > 0)
io.print("cpu>=1:", os.cpu_count() >= 1)
io.print("system true:", os.system("true"))
io.print("system false nonzero:", os.system("false") != 0)
io.print("urandom len:", len(os.urandom(32)))
io.print("urandom varies:", os.urandom(16) != os.urandom(16))

# --- teardown: remove files and dirs ----------------------------------------
io.print("remove f2:", os.remove(f2))
os.rmdir(data)
os.rmdir(nested)
os.rmdir(os.path.join(root, "a", "b"))
os.rmdir(os.path.join(root, "a"))
os.rmdir(root)
io.print("root gone:", os.path.exists(root))
)PURR",
                   "isdir nested: True\n"
                   "isdir data: True\n"
                   "exists f1: True\n"
                   "isfile f1: True\n"
                   "isdir  f1: False\n"
                   "getsize f1: 5\n"
                   "isfile f2: True\n"
                   "isfile f1 gone: False\n"
                   "listdir len: 1\n"
                   "listdir[0]: renamed.txt\n"
                   "cwd basename: data\n"
                   "join: x/y/z.txt\n"
                   "basename: r.dat\n"
                   "dirname: /p/q\n"
                   "normpath: /p/r\n"
                   "abspath: /already/abs\n"
                   "splitext: archive.tar .gz\n"
                   "getenv set: purr\n"
                   "getenv fallback: none\n"
                   "pid positive: True\n"
                   "cpu>=1: True\n"
                   "system true: 0\n"
                   "system false nonzero: True\n"
                   "urandom len: 32\n"
                   "urandom varies: True\n"
                   "remove f2: True\n"
                   "root gone: False\n");
}
