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
 * @return the absolute cwd.
 * @complexity O(n) + a syscall.
 * @alloc allocates the result string.
 * @test CheatahOs.CwdAndCpuCount
 */
std::string getcwd();
/**
 * Change the working directory.
 * @param path the target directory.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.MakedirsAndChdir
 */
void chdir(const std::string& path);
/**
 * List a directory's entries (basenames only).
 * @param path the directory (default `.`).
 * @return the entry names.
 * @complexity O(entries) + syscalls.
 * @alloc allocates a vector of strings.
 * @test CheatahOs.ListdirAndRename
 */
std::vector<std::string> listdir(const std::string& path = ".");
/**
 * Create a single directory.
 * @param path the directory to create.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.MakeDirExistsThenRemove
 */
void mkdir(const std::string& path);
/**
 * Create a directory and any missing parents.
 * @param path the nested directory to create.
 * @complexity O(depth) + syscalls.
 * @alloc none.
 * @test CheatahOs.MakedirsAndChdir
 */
void makedirs(const std::string& path);
/**
 * Remove an (empty) directory.
 * @param path the directory to remove.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.MakeDirExistsThenRemove
 */
void rmdir(const std::string& path);
/**
 * Remove a file or empty directory.
 * @param path the entry to remove.
 * @return true iff something was removed.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.FileQueriesIsfileAndGetsize
 */
bool remove(const std::string& path);  // true if a file was removed
/**
 * Rename/move @p src to @p dst.
 * @param src source path.
 * @param dst destination path.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.ListdirAndRename
 */
void rename(const std::string& src, const std::string& dst);

/**
 * Read an environment variable.
 * @param name the variable name.
 * @param fallback returned when unset.
 * @return the value, or @p fallback.
 * @complexity O(1) + a syscall.
 * @alloc allocates the returned string.
 * @test CheatahOs.GetenvFallback, CheatahOs.SetenvThenGetenv
 */
std::string getenv(const std::string& name, const std::string& fallback = "");
/**
 * Set an environment variable.
 * @param name the variable name.
 * @param value the value to set.
 * @param overwrite replace an existing value when true.
 * @complexity O(1) + a syscall.
 * @alloc may allocate inside the C library's environment table.
 * @test CheatahOs.SetenvThenGetenv
 */
void setenv(const std::string& name, const std::string& value, bool overwrite = true);

/**
 * Process id.
 * @return the current process's pid.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.PidAndSystem
 */
int getpid();
/**
 * Logical CPU count.
 * @return the number of hardware threads (0 if undetermined).
 * @complexity O(1).
 * @alloc none.
 * @test CheatahOs.CwdAndCpuCount
 */
unsigned cpu_count();
/**
 * Run a shell command.
 * @param command the command line.
 * @return the command's exit status.
 * @complexity O(1) here + the cost of the spawned process (fork/exec via the shell).
 * @alloc none.
 * @test CheatahOs.PidAndSystem
 */
int system(const std::string& command);

/// os.path — the path-manipulation submodule.
namespace path {

/**
 * Join path components with the platform separator.
 * @param first the first component.
 * @param rest any further string-constructible components.
 * @return e.g. `join("a","b","c") -> "a/b/c"`.
 * @complexity O(total length).
 * @alloc allocates the result string and per-part path temporaries.
 * @test CheatahOs.PathJoin
 */
template <StringLike... Parts>
std::string join(const std::string& first, const Parts&... rest) {
    std::filesystem::path p(first);
    ((p /= std::filesystem::path(std::string(rest))), ...);
    return p.string();
}

/**
 * Path existence test.
 * @param p the path.
 * @return true iff @p p exists.
 * @complexity O(n) + a syscall.
 * @alloc none.
 * @test CheatahOs.MakeDirExistsThenRemove
 */
bool exists(const std::string& p);
/**
 * Regular-file test.
 * @param p the path.
 * @return true iff @p p is a regular file.
 * @complexity O(n) + a syscall.
 * @alloc none.
 * @test CheatahOs.FileQueriesIsfileAndGetsize
 */
bool isfile(const std::string& p);
/**
 * Directory test.
 * @param p the path.
 * @return true iff @p p is a directory.
 * @complexity O(n) + a syscall.
 * @alloc none.
 * @test CheatahOs.MakeDirExistsThenRemove
 */
bool isdir(const std::string& p);
/**
 * Final path component.
 * @param p the path.
 * @return the basename (filename).
 * @complexity O(n).
 * @alloc allocates a path temporary and the result string.
 * @test CheatahOs.PathBasenameDirname
 */
std::string basename(const std::string& p);
/**
 * Parent path.
 * @param p the path.
 * @return the directory portion of @p p.
 * @complexity O(n).
 * @alloc allocates a path temporary and the result string.
 * @test CheatahOs.PathBasenameDirname
 */
std::string dirname(const std::string& p);
/**
 * Absolute path.
 * @param p the path.
 * @return @p p resolved against the cwd.
 * @complexity O(n) + a syscall (reads the cwd).
 * @alloc allocates the result string.
 * @test CheatahOs.AbspathAndNormpath
 */
std::string abspath(const std::string& p);
/**
 * Lexically normalized path (collapses current-dir and parent-dir segments).
 * @param p the path.
 * @return the normalized path.
 * @complexity O(n) (purely lexical, no syscall).
 * @alloc allocates a path temporary and the result string.
 * @test CheatahOs.AbspathAndNormpath
 */
std::string normpath(const std::string& p);
/**
 * File size in bytes.
 * @param p the file path.
 * @return @p p's size.
 * @complexity O(1) + a syscall.
 * @alloc none.
 * @test CheatahOs.FileQueriesIsfileAndGetsize
 */
std::uintmax_t getsize(const std::string& p);

/**
 * Split a path into root and extension.
 * @param p the path.
 * @return e.g. `splitext("dir/file.purr") -> {"dir/file", ".purr"}` (empty extension when
 *   none).
 * @complexity O(n).
 * @alloc allocates the two result strings and a path temporary.
 * @test CheatahOs.PathSplitext
 */
std::pair<std::string, std::string> splitext(const std::string& p);

} // namespace path
} // namespace cheatah::os
