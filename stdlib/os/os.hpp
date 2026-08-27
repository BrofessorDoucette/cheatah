// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file os.hpp
 * @brief cheatah `os` — Python-like operating-system interface over
 *        `std::filesystem`, plus the `os.path` submodule. `import os` to use it.
 *
 * `import os` includes this header AND links `libcheatah_os`. Templated entry
 * points (e.g. `os.path.join`) live here; the rest is compiled into the library.
 * Unit tests: `stdlib/tests/os_test.cpp`; the suite runs under AddressSanitizer
 * (the `asan` preset) and Valgrind (`security/run-valgrind.sh`) on every QA-gate
 * run.
 *
 * @note Most calls touch the filesystem/environment, so they perform a syscall
 *       in addition to the cost noted per function; `n` is the path length.
 */
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace cheatah::os {

/// StringLike<T>: a std::string can be constructed from T — exactly what
/// os.path.join() does (std::string(part)). Naming it yields a clear "constraint
/// StringLike not satisfied" message while still accepting everything it does today
/// (const char*, char arrays, std::string, std::string_view, …).
template <typename T>
concept StringLike = requires(const T& value) { std::string(value); };

/**
 * Current working directory.
 *
 * Queries the process's current directory via `std::filesystem::current_path`
 * and returns it as an absolute path string.
 * @return the absolute cwd.
 * @complexity O(n) + a syscall.
 * @alloc allocates the result string.
 * @test CheatahOs.CwdAndCpuCount
 * @crtest OsCompileRun.Getcwd
 * @systest StdlibE2E.Os
 */
std::string getcwd();
/**
 * Change the working directory.
 *
 * Sets the process's current directory; subsequent relative paths resolve
 * against it. Throws if @p path does not exist or is not a directory.
 * @param path the target directory.
 * @complexity O(1) + a syscall.
 * @alloc allocates a path temporary.
 * @test CheatahOs.MakedirsAndChdir
 * @crtest OsCompileRun.Chdir
 * @systest StdlibE2E.Os
 */
void chdir(const std::string& path);
/**
 * List a directory's entries (basenames only).
 *
 * Iterates @p path and returns each entry's filename component (not a full
 * path), in unspecified order; `.` and `..` are not included. Throws if @p path
 * does not exist or is not a directory.
 * @param path the directory (default `.`).
 * @return the entry names.
 * @complexity O(entries) + syscalls.
 * @alloc allocates a vector of strings.
 * @test CheatahOs.ListdirAndRename
 * @crtest OsCompileRun.Listdir
 * @systest StdlibE2E.Os
 */
std::vector<std::string> listdir(const std::string& path = ".");
/**
 * Create a single directory.
 *
 * Creates the leaf directory only; the parent must already exist (use
 * makedirs to create missing parents). Does nothing if @p path already exists
 * as a directory.
 * @param path the directory to create.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.MakeDirExistsThenRemove
 * @crtest OsCompileRun.Mkdir
 * @systest StdlibE2E.Os
 */
void mkdir(const std::string& path);
/**
 * Create a directory and any missing parents.
 *
 * Creates @p path along with every intermediate directory that does not yet
 * exist. Succeeds without error if the full path already exists as a directory.
 * @param path the nested directory to create.
 * @complexity O(depth) + syscalls.
 * @alloc none.
 * @test CheatahOs.MakedirsAndChdir
 * @crtest OsCompileRun.Makedirs
 * @systest StdlibE2E.Os
 */
void makedirs(const std::string& path);
/**
 * Remove an (empty) directory.
 *
 * Deletes a single, empty directory; throws if @p path is non-empty. A missing
 * @p path is a no-op (no error). Note this is the same `fs::remove` used by
 * remove(), so it will also delete a regular file at @p path.
 * @param path the directory to remove.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.MakeDirExistsThenRemove
 * @crtest OsCompileRun.Rmdir
 * @systest StdlibE2E.Os
 */
void rmdir(const std::string& path);
/**
 * Remove a file or empty directory.
 *
 * Deletes a single file or empty directory and returns whether anything was
 * removed; a missing @p path returns false rather than throwing. Throws if
 * @p path is a non-empty directory.
 * @param path the entry to remove.
 * @return true iff something was removed.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.FileQueriesIsfileAndGetsize
 * @crtest OsCompileRun.Remove
 * @systest StdlibE2E.Os
 */
bool remove(const std::string& path);  // true if a file was removed
/**
 * Rename/move @p src to @p dst.
 *
 * Moves or renames an entry; an existing @p dst is overwritten when permitted
 * by the underlying `fs::rename`. Crossing filesystems or other failures throw.
 * @param src source path.
 * @param dst destination path.
 * @complexity O(1) + a syscall.
 * @alloc allocates two path temporaries.
 * @test CheatahOs.ListdirAndRename
 * @crtest OsCompileRun.Rename
 * @systest StdlibE2E.Os
 */
void rename(const std::string& src, const std::string& dst);

/**
 * Read an environment variable.
 *
 * Returns @p fallback (default `""`) when the variable is unset; an empty
 * string result therefore does not distinguish "unset" from "set to empty".
 * @param name the variable name.
 * @param fallback returned when unset.
 * @return the value, or @p fallback.
 * @complexity O(environment size) — `std::getenv` is a linear scan of the C library's
 *   environment table (no syscall).
 * @alloc allocates the returned string.
 * @concurrency reads the process-wide environment; a concurrent setenv() on another thread is a data race.
 * @test CheatahOs.GetenvFallback, CheatahOs.SetenvThenGetenv
 * @crtest OsCompileRun.Getenv
 * @systest StdlibE2E.Os
 */
std::string getenv(const std::string& name, const std::string& fallback = "");
/**
 * Set an environment variable.
 *
 * When @p overwrite is false and the variable already exists, the existing
 * value is kept; otherwise it is created or replaced. The change affects only
 * this process and its future children.
 * @param name the variable name.
 * @param value the value to set.
 * @param overwrite replace an existing value when true.
 * @complexity O(environment size) — the C library scans and updates its environment
 *   table (no syscall).
 * @alloc may allocate inside the C library's environment table.
 * @concurrency mutates the process-wide environment; unsafe alongside a concurrent getenv()/setenv() on any thread.
 * @test CheatahOs.SetenvThenGetenv
 * @crtest OsCompileRun.Setenv
 * @systest StdlibE2E.Os
 */
void setenv(const std::string& name, const std::string& value, bool overwrite = true);

/**
 * Process id.
 * @return the current process's pid.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.PidAndSystem
 * @crtest OsCompileRun.Getpid
 * @systest StdlibE2E.Os
 */
int getpid();
/**
 * Logical CPU count.
 *
 * Reports `std::thread::hardware_concurrency()`, the number of concurrent
 * threads supported; the standard allows it to return 0 when the value cannot
 * be determined, so callers should treat 0 as "unknown".
 * @return the number of hardware threads (0 if undetermined).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahOs.CwdAndCpuCount
 * @crtest OsCompileRun.CpuCount
 * @systest StdlibE2E.Os
 */
unsigned cpu_count();
/**
 * Run a shell command.
 *
 * Passes @p command to the system shell via `std::system` and blocks until it
 * finishes; the returned status is implementation-defined (on POSIX, a wait
 * status, conventionally decoded so that 0 means success).
 * @param command the command line.
 * @return the command's exit status.
 * @complexity O(1) here + the cost of the spawned process (fork/exec via the shell).
 * @alloc none.
 * @warning @p command is interpreted by the shell (quoting, expansion, `;`/`|`) — never
 *   build it from untrusted input.
 * @test CheatahOs.PidAndSystem
 * @crtest OsCompileRun.System
 * @systest StdlibE2E.Os
 */
int system(const std::string& command);
/**
 * Cryptographically secure random bytes (like Python's `os.urandom`).
 *
 * Reads @p n bytes from the operating system's CSPRNG — `getentropy`/`/dev/urandom`
 * on POSIX, `BCryptGenRandom` on Windows — suitable for keys and signatures. Unlike
 * the `random` module (a deterministic, seedable PRNG), this is NOT reproducible and
 * must not be seeded. Throws `std::runtime_error` if the OS source cannot be read
 * (so a key is never built from non-random bytes), and `std::invalid_argument` for a
 * negative @p n.
 * @param n the number of bytes to return (must be non-negative).
 * @return a string of @p n random bytes (may contain embedded NULs).
 * @complexity O(n), plus one syscall per 256-byte chunk on POSIX (getentropy's
 *   per-call limit; a single BCryptGenRandom call on Windows).
 * @alloc allocates the n-byte result.
 * @test CheatahOs.Urandom
 * @crtest OsCompileRun.Urandom
 * @systest StdlibE2E.Os
 */
std::string urandom(int n);

/**
 * The loadable-module file extension for this platform.
 *
 * A compiled cheatah program is a native loadable module run by the `cheatah`
 * host; its file extension is `.so` on Linux/BSD, `.dylib` on macOS, and `.dll`
 * on Windows. Tools that build or name modules (e.g. the `biome` package manager)
 * use this instead of hardcoding `.so`, so the paths they print and generate are
 * correct on every platform. The result includes the leading dot.
 * @return the platform module extension (e.g. `".so"`, `".dylib"`, `".dll"`).
 * @complexity O(1).
 * @alloc allocates the returned string.
 * @test CheatahOs.ModuleExt
 * @crtest OsCompileRun.ModuleExt
 * @systest StdlibE2E.Os
 */
std::string module_ext();

/// os.path — the path-manipulation submodule.
namespace path {

/**
 * Join path components with the platform separator.
 *
 * Appends each component with `path::operator/=`, inserting a separator as
 * needed; following `std::filesystem` rules, an absolute component discards
 * everything joined before it. Purely lexical — the filesystem is not touched.
 * @param first the first component.
 * @param rest any further string-constructible components.
 * @return e.g. `join("a","b","c") -> "a/b/c"`.
 * @complexity O(total length).
 * @alloc allocates the result string and per-part path temporaries.
 * @test CheatahOs.PathJoin
 * @crtest OsCompileRun.PathJoin
 * @systest StdlibE2E.Os
 */
template <StringLike... Parts>
std::string join(const std::string& first, const Parts&... rest) {
    std::filesystem::path p(first);
    ((p /= std::filesystem::path(std::string(rest))), ...);
    return p.string();
}

/**
 * Path existence test.
 *
 * Follows symlinks and is true for any existing entry — file, directory, or
 * other; returns false for a missing path.
 * @param p the path.
 * @return true iff @p p exists.
 * @complexity O(n) + a syscall.
 * @alloc none.
 * @warning The answer is a snapshot: the entry can be created or removed between this
 *   check and any subsequent use (TOCTOU) — do not rely on it as a security check.
 * @test CheatahOs.MakeDirExistsThenRemove
 * @crtest OsCompileRun.PathExists
 * @systest StdlibE2E.Os
 */
bool exists(const std::string& p);
/**
 * Regular-file test.
 *
 * Returns false (rather than throwing) when @p p is missing or is a non-regular
 * entry such as a directory; symlinks are followed to their target.
 * @param p the path.
 * @return true iff @p p is a regular file.
 * @complexity O(n) + a syscall.
 * @alloc none.
 * @test CheatahOs.FileQueriesIsfileAndGetsize
 * @crtest OsCompileRun.PathIsfile
 * @systest StdlibE2E.Os
 */
bool isfile(const std::string& p);
/**
 * Directory test.
 *
 * Returns false (rather than throwing) when @p p is missing or is not a
 * directory; symlinks are followed to their target.
 * @param p the path.
 * @return true iff @p p is a directory.
 * @complexity O(n) + a syscall.
 * @alloc none.
 * @test CheatahOs.MakeDirExistsThenRemove
 * @crtest OsCompileRun.PathIsdir
 * @systest StdlibE2E.Os
 */
bool isdir(const std::string& p);
/**
 * Final path component.
 *
 * Returns the trailing filename component lexically, without touching the
 * filesystem; a path ending in a separator (e.g. `a/b/`) yields an empty
 * string, matching `std::filesystem::path::filename`.
 * @param p the path.
 * @return the basename (filename).
 * @complexity O(n).
 * @alloc allocates a path temporary and the result string.
 * @test CheatahOs.PathBasenameDirname
 * @crtest OsCompileRun.PathBasename
 * @systest StdlibE2E.Os
 */
std::string basename(const std::string& p);
/**
 * Parent path.
 *
 * Returns everything before the final component lexically, without touching the
 * filesystem; a bare filename with no separator (e.g. `file.txt`) yields an
 * empty string, matching `std::filesystem::path::parent_path`.
 * @param p the path.
 * @return the directory portion of @p p.
 * @complexity O(n).
 * @alloc allocates a path temporary and the result string.
 * @test CheatahOs.PathBasenameDirname
 * @crtest OsCompileRun.PathDirname
 * @systest StdlibE2E.Os
 */
std::string dirname(const std::string& p);
/**
 * Absolute path.
 *
 * Prepends the current working directory to a relative @p p; it does not
 * collapse `.`/`..` segments or resolve symlinks (combine with normpath for
 * that), and @p p need not exist.
 * @param p the path.
 * @return @p p resolved against the cwd.
 * @complexity O(n) + a syscall (reads the cwd).
 * @alloc allocates the result string.
 * @test CheatahOs.AbspathAndNormpath
 * @crtest OsCompileRun.PathAbspath
 * @systest StdlibE2E.Os
 */
std::string abspath(const std::string& p);
/**
 * Lexically normalized path (collapses current-dir and parent-dir segments).
 * @param p the path.
 * @return the normalized path.
 * @complexity O(n) (purely lexical, no syscall).
 * @alloc allocates a path temporary and the result string.
 * @test CheatahOs.AbspathAndNormpath
 * @crtest OsCompileRun.PathNormpath
 * @systest StdlibE2E.Os
 */
std::string normpath(const std::string& p);
/**
 * File size in bytes.
 *
 * Defined only for regular files; querying a missing path, or a directory or
 * other non-regular entry, throws rather than returning a sentinel.
 * @param p the file path.
 * @return @p p's size.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.FileQueriesIsfileAndGetsize
 * @crtest OsCompileRun.PathGetsize
 * @systest StdlibE2E.Os
 */
std::uintmax_t getsize(const std::string& p);

/**
 * Split a path into root and extension.
 *
 * Splits at the last dot of the final component so that concatenating the two
 * results reproduces @p p; when there is no extension the whole path is the
 * root and the extension is empty. The extension includes its leading dot, and
 * a leading-dot name (e.g. `.bashrc`) is treated as having no extension.
 * @param p the path.
 * @return e.g. `splitext("dir/file.purr") -> {"dir/file", ".purr"}` (empty extension when
 *   none).
 * @complexity O(n).
 * @alloc allocates the two result strings and a path temporary.
 * @test CheatahOs.PathSplitext
 * @crtest OsCompileRun.PathSplitext
 * @systest StdlibE2E.Os
 */
std::pair<std::string, std::string> splitext(const std::string& p);

} // namespace path
} // namespace cheatah::os
