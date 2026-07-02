// callback_sys_test.cpp — SYSTEM tests for cheatah's function-value feature: a
// `fn` passed as an argument lowers to a callable that binds to a C++
// `std::function<...>` parameter, so hand-written C++ modules can accept cheatah
// callbacks and invoke them (including from a background thread).
//
// Each test builds a small, GENERIC hand-written C++ module (no project-specific
// content), writes a .purr that imports it and passes plain `fn`s as callbacks,
// compiles through the real purrc -> cheatah pipeline, runs it, and checks
// stdout. Rich argument/return types are exercised: float, str, list, dict,
// ndarray — passed in and returned.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include <sys/stat.h>

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

int run(const std::string& cmd) { return std::system((cmd + " >/dev/null 2>&1").c_str()); }

void write_file(const std::string& p, const std::string& c) {
    std::ofstream f(p);
    f << c;
}

// Compile + run a .purr that imports a header-only C++ module staged under `root`,
// and return its captured stdout (empty on pipeline failure).
std::string compile_and_run(const std::string& root, const std::string& prog,
                            const std::string& mod, int& rc) {
    const std::string compile =
        "CHEATAH_MODULE_PATH=" + root + " \"" + std::string(PURRC_PATH) + "\" \"" + prog +
        "\" -o \"" + mod + "\"";
    if (std::system((compile + " >/dev/null 2>&1").c_str()) != 0) { rc = -2; return {}; }
    FILE* pipe = popen((std::string(CHEATAH_RUNTIME_PATH) + " \"" + mod + "\" 2>/dev/null").c_str(), "r");
    if (pipe == nullptr) { rc = -3; return {}; }
    std::string out;
    char buf[256];
    while (std::fgets(buf, sizeof buf, pipe) != nullptr) out += buf;
    rc = pclose(pipe);
    return out;
}

// A header-only GENERIC C++ module: each function takes a cheatah callback as a
// std::function and invokes it. `cheatah-deps: ndarray` links ndarray for the
// consumer; `cheatah-link: -lpthread` forwards pthread for the threaded call.
const char* kCbkitHeader = R"HPP(#pragma once
// cheatah-deps: ndarray
// cheatah-link: -lpthread
#include <functional>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include "ndarray.hpp"
namespace cheatah::cbkit {
using NDArray = ::cheatah::ndarray::NDArray;
inline double apply_d(std::function<double(double)> f, double x) { return f(x); }
inline std::string apply_s(std::function<std::string(std::string)> f, std::string s) { return f(s); }
inline long long apply_list(std::function<long long(std::vector<long long>)> f,
                            std::vector<long long> v) { return f(v); }
inline long long apply_dict(std::function<long long(std::unordered_map<std::string, long long>)> f,
                            std::unordered_map<std::string, long long> m) { return f(m); }
// Pass an ndarray IN and get one back OUT, then reduce it (proves NDArray round-trips a callback).
inline double sum_nd(std::function<NDArray(NDArray)> f, NDArray x) {
    NDArray y = f(x);
    double s = 0.0;
    for (long long i = 0; i < static_cast<long long>(y.size()); ++i)
        s += ::cheatah::ndarray::get(y, {i});
    return s;
}
// Invoke the callback from a BACKGROUND thread — the case a brain's decision
// thread relies on. The capture-less wrapper purrc emits is safe to call here.
inline double apply_threaded(std::function<double(double)> f, double x) {
    double out = 0.0;
    std::thread t([&] { out = f(x); });
    t.join();
    return out;
}
}  // namespace cheatah::cbkit
)HPP";

// Stage the cbkit module (header + checksum sidecar) under a fresh root and return it.
std::string stage_cbkit(const std::string& tag) {
    const std::string root = std::string(PURR_TEST_TMP) + "/cb_" + tag;
    const std::string dir = root + "/cbkit";
    run("rm -rf " + root + " && mkdir -p " + dir);
    write_file(dir + "/cbkit.hpp", kCbkitHeader);
    // sha512 sidecar (run() appends >/dev/null, so wrap the redirect in sh -c).
    run("sh -c 'cd " + dir + " && sha512sum cbkit.hpp > cbkit.hpp.sha512'");
    return root;
}

}  // namespace

// A plain `fn` passed by name binds to a std::function and round-trips rich types.
TEST(Callback, RichTypesThroughStdFunction) {
    const std::string root = stage_cbkit("rich");
    const std::string prog = root + "/prog.purr", mod = root + "/prog.so";
    write_file(prog, R"PURR(import io
import ndarray
import cbkit

fn dbl(x : float) {
    return x * 2.0
}
fn shout(s : str) {
    return s + "!"
}
fn total(v : list<int>) {
    let s = 0
    for e in v {
        s += e
    }
    return s
}
fn lookup(m : dict<str, int>) {
    return m["k"]
}
fn keep(a : ndarray<float>) {
    return a
}

fn main() {
    let ok = true
    if cbkit.apply_d(dbl, 21.0) != 42.0 {
        ok = false
    }
    if cbkit.apply_s(shout, "hi") != "hi!" {
        ok = false
    }
    if cbkit.apply_list(total, [1, 2, 3, 4]) != 10 {
        ok = false
    }
    if cbkit.apply_dict(lookup, {"k": 7}) != 7 {
        ok = false
    }
    if cbkit.sum_nd(keep, ndarray.array([1.0, 2.0, 3.0])) != 6.0 {
        ok = false
    }
    if ok {
        io.print("ok")
    } else {
        io.print("bad")
    }
}

main()
)PURR");
    int rc = -1;
    const std::string out = compile_and_run(root, prog, mod, rc);
    EXPECT_EQ(rc, 0) << "callback program pipeline failed";
    EXPECT_EQ(out, "ok\n");
}

// The callback must be invokable from a C++ BACKGROUND THREAD (capture-less wrapper).
TEST(Callback, InvokedFromBackgroundThread) {
    const std::string root = stage_cbkit("thread");
    const std::string prog = root + "/prog.purr", mod = root + "/prog.so";
    write_file(prog, R"PURR(import io
import cbkit

fn tenx(x : float) {
    return x * 10.0
}

fn main() {
    if cbkit.apply_threaded(tenx, 5.0) == 50.0 {
        io.print("ok")
    } else {
        io.print("bad")
    }
}

main()
)PURR");
    int rc = -1;
    const std::string out = compile_and_run(root, prog, mod, rc);
    EXPECT_EQ(rc, 0) << "threaded callback pipeline failed";
    EXPECT_EQ(out, "ok\n");
}
