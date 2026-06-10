// System-level tests for the generated-code namespace aliasing. The whole program is
// emitted inside a `namespace cheatah_program { … }`, where each stdlib module gets its
// OWN short alias (`namespace io = ::cheatah::io;`, `namespace random = …`, …) so the
// body reads `io::print` / `random::randint` instead of `cheatah::io::print`. The
// wrapper namespace is what makes this SAFE: an alias like `random`/`time`/`socket`
// cannot collide with the global C function of the same name.
//
// These run the .purr through the REAL purrc -> cheatah pipeline and assert on BOTH:
//   1. the generated C++ (the feature itself — that each module is aliased distinctly), and
//   2. the program's runtime output (that the aliased code still compiles and behaves).
//
// Named NamespaceAliasing (not *CompileRun*) so the QA gate always runs them.
#include "e2e_harness.hpp"

static bool has(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// No collision: every imported module — plus the always-available builtins — gets its
// own distinct alias, and the body uses the short form. The program still runs.
TEST(NamespaceAliasing, EachModuleGetsItsOwnAlias) {
    const std::string gen = e2e::expect_e2e_source("ns_distinct", R"PURR(import io
import math
io.print(math.sqrt(16.0), math.floor(2.7), len("hi"))
)PURR", "4 2 2\n");
    EXPECT_TRUE(has(gen, "namespace cheatah_program {"));
    // Distinct alias per module — never `cheatah::io::` / `cheatah::math::` in the body.
    EXPECT_TRUE(has(gen, "namespace io = ::cheatah::io;"));
    EXPECT_TRUE(has(gen, "namespace math = ::cheatah::math;"));
    EXPECT_TRUE(has(gen, "namespace builtins = ::cheatah::builtins;"));
    EXPECT_TRUE(has(gen, "io::print("));
    EXPECT_TRUE(has(gen, "math::sqrt("));
    EXPECT_TRUE(has(gen, "builtins::len("));
    EXPECT_FALSE(has(gen, "cheatah::io::print"));
    EXPECT_FALSE(has(gen, "cheatah::math::sqrt"));
}

// Modules whose names match a global C library function (random -> ::random,
// time -> ::time) are STILL aliased — the wrapper namespace prevents the clash. If the
// alias leaked to global scope this would not compile ("redefinition of 'random'").
TEST(NamespaceAliasing, LibcNamedModulesAreAliasedSafely) {
    const std::string gen = e2e::expect_e2e_source("ns_libc", R"PURR(import io
import random
random.seed(123)
io.print(random.randint(1, 1))
)PURR", "1\n");
    EXPECT_TRUE(has(gen, "namespace random = ::cheatah::random;"));
    EXPECT_TRUE(has(gen, "random::seed("));
    EXPECT_TRUE(has(gen, "random::randint("));
    EXPECT_FALSE(has(gen, "cheatah::random::randint"));
}

// Collision with a PROGRAM identifier: a `struct os` shares the imported `os` module's
// name, so os must NOT be aliased (it would clash with the struct inside the namespace).
// It stays explicit (`::cheatah::os::…`) while the non-colliding io is still aliased —
// and the program compiles and runs.
TEST(NamespaceAliasing, ProgramNameCollisionStaysExplicit) {
    const std::string gen = e2e::expect_e2e_source("ns_conflict", R"PURR(import io
import os
struct os { n: int }
let p = os(7)
io.print(os.path.join("a", "b"), p.n)
)PURR", "a/b 7\n");
    EXPECT_TRUE(has(gen, "namespace io = ::cheatah::io;"));     // safe -> aliased
    EXPECT_FALSE(has(gen, "namespace os = ::cheatah::os;"));    // collides -> not aliased
    EXPECT_TRUE(has(gen, "::cheatah::os::path::join"));         // stays explicit
}

// A program function named like the generated entry point (`fn run`, `fn purr_main`)
// must NOT collide with the internal trampoline function — the entry name escalates
// until it's free, so the program compiles and runs.
TEST(NamespaceAliasing, EntryFunctionNameNeverCollides) {
    const std::string gen = e2e::expect_e2e_source("ns_entry", R"PURR(import io
fn run() {
    io.print("ran")
}
fn purr_main() {
    run()
}
purr_main()
)PURR", "ran\n");
    // The exported C entry calls the escalated trampoline, NOT the user's run()/purr_main().
    EXPECT_TRUE(has(gen, "PURR_EXPORT void purr_main() { cheatah_program::purr_main_(); }"));
    EXPECT_FALSE(has(gen, "cheatah_program::run()"));
}

// A function parameter named like an imported module shadows it: the bare `math` is the
// parameter (not the module namespace), aliasing is skipped, and the program still runs.
TEST(NamespaceAliasing, ParameterNameShadowsModule) {
    const std::string gen = e2e::expect_e2e_source("ns_param", R"PURR(import io
import math
fn bump(math) {
    return math + 1
}
io.print(bump(41))
)PURR", "42\n");
    EXPECT_FALSE(has(gen, "namespace math = ::cheatah::math;"));  // shadowed -> not aliased
    EXPECT_TRUE(has(gen, "return (math + 1LL);"));                // bare param, not a namespace
}
