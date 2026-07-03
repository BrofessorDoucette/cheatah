// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file sys.hpp
 * @brief cheatah `sys` — system-specific parameters, primarily the command-line
 *        arguments. `import sys` to use it.
 *
 * `import sys` includes this header AND links `libcheatah_sys`. The headline
 * member is `sys.argv`, the list of strings passed to the program on the command
 * line — `sys.argv[0]` is the program (module) name, `sys.argv[1:]` the user
 * arguments, exactly like Python.
 *
 * `sys.argv` is populated by the `cheatah` runtime: when it runs a module
 * (`cheatah <program> [args…]`) it forwards the arguments to the loaded module
 * through the exported `cheatah_set_argv` hook (which this module provides) before
 * calling `purr_main`. A program reads `sys.argv`; it never calls the hook itself.
 *
 * Unit tests: `stdlib/tests/sys_test.cpp`; the suite runs under AddressSanitizer
 * (the `asan` preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate
 * run.
 */
#include <string>
#include <vector>

namespace cheatah::sys {

/**
 * The program's command-line arguments.
 *
 * `argv[0]` is the program (module) name as the runtime was given it; `argv[1]`
 * onward are the user arguments. Populated by the cheatah runtime before the
 * program runs. Read it directly (`sys.argv`, `sys.argv[1]`, `len(sys.argv)`,
 * `for a in sys.argv { … }`).
 * @test CheatahSys.Argv
 * @systest StdlibE2E.Sys
 */
extern std::vector<std::string> argv;

/// @cond INTERNAL — a runtime hook, not cheatah surface (programs read `sys.argv`)
/**
 * Capture the process arguments into `argv`.
 *
 * Called once, before the program body, by the cheatah runtime (through the
 * exported `cheatah_set_argv` hook) to copy `argc`/`argv` into the `argv` vector.
 * Not part of the cheatah surface — programs read `sys.argv`, they do not call this.
 * @param argc the standard C argument count.
 * @param argv_ the standard C argument vector (`argc` valid entries).
 * @complexity O(argc · m) for argument length m.
 * @alloc allocates the argument strings.
 * @test CheatahSys.Argv
 */
void set_argv(int argc, char** argv_);
/// @endcond

} // namespace cheatah::sys
