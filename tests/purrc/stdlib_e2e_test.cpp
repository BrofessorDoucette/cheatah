// System-level tests for every standard-library module: write a real .purr
// program that imports the module, compile it with purrc into a loadable module,
// run it with the cheatah runtime, and confirm the exact stdout.
//
// These are intentionally end-to-end (purrc + the C++ backend + the runtime +
// the linked stdlib library), complementing the in-process unit tests in
// stdlib/tests/. Every program prints a DETERMINISTIC line so the output can be
// asserted byte-for-byte (no clocks/PIDs/unseeded RNG; datetime runs under
// TZ=UTC; RNG is seeded; floats use io.print's default 6-significant-digit form).

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include <sys/wait.h>

#include <gtest/gtest.h>

#ifndef PURRC_PATH
#define PURRC_PATH ""
#endif
#ifndef CHEATAH_RUNTIME_PATH
#define CHEATAH_RUNTIME_PATH ""
#endif
#ifndef PURR_TEST_TMP
#define PURR_TEST_TMP "."
#endif

namespace {

std::string run_capture(const std::string& cmd, int& exit_code) {
    std::string out;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (pipe == nullptr) {
        exit_code = -1;
        return out;
    }
    std::array<char, 256> buf{};
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr) {
        out += buf.data();
    }
    const int status = pclose(pipe);
    exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return out;
}

// Compile `src` with purrc, run the module under the cheatah runtime (optionally
// with an env prefix like "TZ=UTC "), and assert the captured stdout equals
// `expected`. `name` makes the temp files and failure messages unique per module.
void expect_e2e(const std::string& name, const std::string& src, const std::string& expected,
                const std::string& env_prefix = "") {
    const std::string tmp = PURR_TEST_TMP;
    const std::string purr = tmp + "/" + name + "_e2e.purr";
    const std::string mod = tmp + "/" + name + "_e2e.so";
    { std::ofstream f(purr); f << src; }

    const std::string compile =
        std::string(PURRC_PATH) + " \"" + purr + "\" -o \"" + mod + "\"";
    ASSERT_EQ(std::system(compile.c_str()), 0) << name << ": purrc failed to compile the program";

    int rc = -1;
    const std::string out =
        run_capture(env_prefix + "\"" + std::string(CHEATAH_RUNTIME_PATH) + "\" \"" + mod + "\"", rc);
    EXPECT_EQ(rc, 0) << name << ": program exited non-zero";
    EXPECT_EQ(out, expected) << name << ": stdout mismatch";
}

}  // namespace

TEST(StdlibE2E, Builtins) {
    expect_e2e("builtins", R"PURR(import io
io.print(len("meow"), ord("A"), chr(66), hex(255), oct(8), bin(5))
)PURR",
               "4 65 B 0xff 0o10 0b101\n");
}

TEST(StdlibE2E, Io) {
    expect_e2e("io", R"PURR(import io
io.print(io.str(42), io.repr("hi"), io.format("{}+{}={}", 1, 2, 3))
)PURR",
               "42 'hi' 1+2=3\n");
}

TEST(StdlibE2E, Os) {
    expect_e2e("os", R"PURR(import io
import os
io.print(os.path.basename("/a/b/c.txt"), os.path.dirname("/a/b/c.txt"), os.path.normpath("/a/./b/../c"))
)PURR",
               "c.txt /a/b /a/c\n");
}

TEST(StdlibE2E, String) {
    expect_e2e("string", R"PURR(import io
import string
io.print(string.upper("meow"), string.replace("a-b-b", "b", "x"), string.count("banana", "a"), string.split("a,b,c", ",")[1])
)PURR",
               "MEOW a-x-x 3 b\n");
}

TEST(StdlibE2E, Math) {
    expect_e2e("math", R"PURR(import io
import math
io.print(math.sqrt(16.0), math.gcd(12, 18), math.factorial(5), math.floor(3.7), math.round(2.6))
)PURR",
               "4 6 120 3 3\n");
}

TEST(StdlibE2E, Time) {
    // Clock values are non-deterministic, so assert monotonic ordering as booleans.
    expect_e2e("time", R"PURR(import io
import time
let t0 = time.perf_counter()
time.sleep(0.01)
let t1 = time.perf_counter()
io.print(t1 >= t0, time.monotonic() > 0.0)
)PURR",
               "True True\n");
}

TEST(StdlibE2E, Datetime) {
    // Components use local time; pin TZ=UTC so epoch 0 is deterministic.
    expect_e2e("datetime", R"PURR(import io
import datetime
io.print(datetime.format(0.0, "%Y-%m-%d %H:%M:%S"), datetime.year(0.0), datetime.weekday(0.0))
)PURR",
               "1970-01-01 00:00:00 1970 3\n", "TZ=UTC ");
}

TEST(StdlibE2E, Random) {
    // Seeded => reproducible; a==b proves seeding, and randint(5,5) is always 5.
    expect_e2e("random", R"PURR(import io
import random
random.seed(42)
let a = random.randint(1, 1000000)
random.seed(42)
let b = random.randint(1, 1000000)
io.print(a == b, random.randint(5, 5))
)PURR",
               "True 5\n");
}

TEST(StdlibE2E, Statistics) {
    expect_e2e("statistics", R"PURR(import io
import statistics
let xs = [1.0, 2.0, 3.0, 4.0, 5.0]
io.print(statistics.mean(xs), statistics.median(xs), statistics.variance(xs))
)PURR",
               "3 3 2.5\n");
}

TEST(StdlibE2E, Hashlib) {
    // Known SHA-256 vector for "abc".
    expect_e2e("hashlib", R"PURR(import io
import hashlib
io.print(hashlib.sha256("abc"))
)PURR",
               "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad\n");
}

TEST(StdlibE2E, Ndarray) {
    expect_e2e("ndarray", R"PURR(import io
import ndarray
let a = ndarray.array([1.0, 2.0, 3.0])
let b = ndarray.add(a, ndarray.scalar(10.0))
io.print(ndarray.to_string(b), ndarray.sum(b))
)PURR",
               "[11, 12, 13] 36\n");
}

TEST(StdlibE2E, Linalg) {
    expect_e2e("linalg", R"PURR(import io
import linalg
import ndarray
let a = ndarray.reshape(ndarray.array([1.0, 2.0, 3.0, 4.0]), [2, 2])
io.print(linalg.det(a), linalg.trace(a))
)PURR",
               "-2 5\n");
}
