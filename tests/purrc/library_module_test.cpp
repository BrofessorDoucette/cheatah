// End-to-end tests for purrc's cheatah-library emitter (`--emit-library`) and the
// consumer-side integrity verification:
//   - a TRANSPARENT module inlines its generated C++ source into a signed header (so the
//     true code is always visible);
//   - an OPAQUE module (the default) ships only the API in the header and hides the
//     implementation in a signed static archive;
//   - a program that `import`s a module verifies the module's SHA-512 checksum before
//     compiling against it, and REFUSES a tampered module (fail-closed).
// See cmake/CheatahModule.cmake and the first-party `parsers` module.
#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

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
bool exists(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0;
}
std::string read_file(const std::string& p) {
    std::ifstream f(p);
    std::ostringstream s;
    s << f.rdbuf();
    return s.str();
}
void write_file(const std::string& p, const std::string& c) {
    std::ofstream f(p);
    f << c;
}
int run(const std::string& cmd) { return std::system((cmd + " >/dev/null 2>&1").c_str()); }

// A fresh per-test module dir <tmp>/<tag>/<name>/, with <name>.purr written into it.
std::string make_module_dir(const std::string& tag, const std::string& name) {
    const std::string root = std::string(PURR_TEST_TMP) + "/libmod_" + tag;
    const std::string dir = root + "/" + name;
    run("rm -rf " + root + " && mkdir -p " + dir);
    write_file(dir + "/" + name + ".purr", "# " + name + " library module (mechanism test)\n");
    return dir;
}
const std::string kPurrc = PURRC_PATH;
}  // namespace

TEST(LibraryModule, TransparentEmitsSignedSourceInHeader) {
    const std::string dir = make_module_dir("t", "foo");
    ASSERT_EQ(run(kPurrc + " --emit-library --transparent " + dir + "/foo.purr -o " + dir + "/foo.hpp"), 0);
    EXPECT_TRUE(exists(dir + "/foo.hpp"));
    EXPECT_TRUE(exists(dir + "/foo.hpp.sha512"));  // signed (checksum sidecar)
    const std::string hdr = read_file(dir + "/foo.hpp");
    EXPECT_NE(hdr.find("namespace cheatah::foo"), std::string::npos);
    EXPECT_NE(hdr.find("inline const char* module_abi()"), std::string::npos);  // source inline
    EXPECT_FALSE(exists(dir + "/libcheatah_foo.a"));  // transparent: no compiled archive
}

TEST(LibraryModule, OpaqueHidesSourceInSignedArchive) {
    const std::string dir = make_module_dir("o", "foo");
    ASSERT_EQ(run(kPurrc + " --emit-library " + dir + "/foo.purr -o " + dir + "/foo.hpp"), 0);  // default = opaque
    const std::string hdr = read_file(dir + "/foo.hpp");
    EXPECT_NE(hdr.find("const char* module_abi() noexcept;"), std::string::npos);  // API only
    EXPECT_EQ(hdr.find("return \"foo\""), std::string::npos);  // implementation NOT in the header
    EXPECT_TRUE(exists(dir + "/libcheatah_foo.a"));            // compiled, hidden, signed
    EXPECT_TRUE(exists(dir + "/libcheatah_foo.a.sha512"));
}

TEST(LibraryModule, ImportVerifiesAndRuns) {
    const std::string dir = make_module_dir("i", "foo");
    const std::string root = dir.substr(0, dir.rfind('/'));  // the search root holding foo/
    ASSERT_EQ(run(kPurrc + " --emit-library --transparent " + dir + "/foo.purr -o " + dir + "/foo.hpp"), 0);
    const std::string prog = root + "/prog.purr", mod = root + "/prog.so";
    write_file(prog, "import io\nimport foo\nio.print(foo.module_abi())\n");
    ASSERT_EQ(run("CHEATAH_MODULE_PATH=" + root + " " + kPurrc + " " + prog + " -o " + mod), 0);
    FILE* pipe = popen((std::string(CHEATAH_RUNTIME_PATH) + " " + mod + " 2>/dev/null").c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::string out;
    char buf[128];
    while (std::fgets(buf, sizeof buf, pipe)) out += buf;
    pclose(pipe);
    EXPECT_EQ(out, "foo\n");
}

TEST(LibraryModule, TamperedModuleFailsClosed) {
    const std::string dir = make_module_dir("x", "foo");
    const std::string root = dir.substr(0, dir.rfind('/'));
    ASSERT_EQ(run(kPurrc + " --emit-library --transparent " + dir + "/foo.purr -o " + dir + "/foo.hpp"), 0);
    {  // tamper the header — its committed checksum no longer matches
        std::ofstream f(dir + "/foo.hpp", std::ios::app);
        f << "\n// tampered\n";
    }
    const std::string prog = root + "/prog.purr", mod = root + "/prog.so";
    write_file(prog, "import io\nimport foo\nio.print(foo.module_abi())\n");
    EXPECT_NE(run("CHEATAH_MODULE_PATH=" + root + " " + kPurrc + " " + prog + " -o " + mod), 0)
        << "a consumer must refuse a module whose checksum no longer matches";
}
