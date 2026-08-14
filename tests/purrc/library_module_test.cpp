// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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

TEST(LibraryModule, CheatahLinkMarkerForwardsExternalLibs) {
    // A hand-written C++ module whose hidden implementation needs a system library
    // declares it with a `// cheatah-link:` header marker; purrc must append that flag
    // to the consumer's link command. Here the impl spawns a std::thread, so it needs
    // -lpthread; without the marker forwarding it the program would fail to link on a
    // toolchain that does not pull pthread in by default.
    const std::string root = std::string(PURR_TEST_TMP) + "/libmod_link";
    const std::string dir = root + "/thr";
    run("rm -rf " + root + " && mkdir -p " + dir);
    write_file(dir + "/thr.hpp",
               "#pragma once\n// cheatah-link: -lpthread\n"
               "namespace cheatah::thr {\nlong long on_thread();\n}\n");
    write_file(root + "/impl.cpp",
               "#include \"thr.hpp\"\n#include <thread>\n"
               "namespace cheatah::thr {\nlong long on_thread() {\n"
               "  long long r = 0;\n  std::thread t([&r]{ r = 7; });\n  t.join();\n  return r;\n}\n}\n");
    ASSERT_EQ(run("g++ -std=c++20 -fPIC -I" + dir + " -c " + root + "/impl.cpp -o " + root + "/impl.o"), 0);
    ASSERT_EQ(run("ar rcs " + dir + "/libcheatah_thr.a " + root + "/impl.o"), 0);
    // Wrap the redirecting checksum commands in `sh -c '...'` so run()'s appended
    // `>/dev/null` redirects the subshell's stdout, not the sidecar file writes.
    ASSERT_EQ(run("sh -c 'cd " + dir + " && sha512sum thr.hpp > thr.hpp.sha512 && "
                  "sha512sum libcheatah_thr.a > libcheatah_thr.a.sha512'"), 0);
    const std::string prog = root + "/prog.purr", mod = root + "/prog.so";
    write_file(prog, "import io\nimport thr\nio.print(thr.on_thread())\n");
    ASSERT_EQ(run("CHEATAH_MODULE_PATH=" + root + " " + kPurrc + " " + prog + " -o " + mod), 0);
    FILE* pipe = popen((std::string(CHEATAH_RUNTIME_PATH) + " " + mod + " 2>/dev/null").c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::string out;
    char buf[64];
    while (std::fgets(buf, sizeof buf, pipe)) out += buf;
    pclose(pipe);
    EXPECT_EQ(out, "7\n");
}

TEST(LibraryModule, DocCommentsBecomeJavadoc) {
    const std::string dir = make_module_dir("d", "foo");
    write_file(dir + "/foo.purr",
               "# foo — a documented module. Exists to prove doc emission.\n"
               "#\n"
               "# Second paragraph of the module doc.\n"
               "\n"
               "# Twice the input.\n"
               "#\n"
               "# @param x the input.\n"
               "# @return 2*x.\n"
               "fn twice(x) {\n"
               "    # a body comment: NOT documentation\n"
               "    return x * 2  # trailing comment: NOT documentation\n"
               "}\n"
               "\n"
               "# A documented struct.\n"
               "struct P {\n"
               "    x: int\n"
               "\n"
               "    # A documented method.\n"
               "    fn get(self) {\n"
               "        return self.x\n"
               "    }\n"
               "}\n"
               "\n"
               "fn undocumented(x) {\n"
               "    return x\n"
               "}\n"
               "\n"
               "# Detached comment (blank line below) — NOT documentation.\n"
               "\n"
               "fn detached(x) {\n"
               "    return x\n"
               "}\n");
    ASSERT_EQ(run(kPurrc + " --emit-library --transparent " + dir + "/foo.purr -o " + dir + "/foo.hpp"), 0);
    const std::string hdr = read_file(dir + "/foo.hpp");

    // Module doc -> @file Javadoc, above the namespace.
    EXPECT_NE(hdr.find("/**\n * @file foo.hpp\n *\n"
                       " * foo — a documented module. Exists to prove doc emission.\n"
                       " *\n"
                       " * Second paragraph of the module doc.\n */"),
              std::string::npos)
        << hdr;
    // fn doc -> Javadoc directly above the emitted function; @tags pass through verbatim.
    // Library free fns are emitted `inline` — multi-TU consumers would hit ODR otherwise.
    EXPECT_NE(hdr.find("/**\n * Twice the input.\n *\n"
                       " * @param x the input.\n * @return 2*x.\n */\ninline auto twice("),
              std::string::npos)
        << hdr;
    // struct + method docs (method Javadoc is indented with its member).
    EXPECT_NE(hdr.find("/**\n * A documented struct.\n */\nstruct P {"), std::string::npos) << hdr;
    EXPECT_NE(hdr.find("    /**\n     * A documented method.\n     */\n"), std::string::npos) << hdr;
    // Non-docs stay out of the header: body/trailing/detached comments, and an
    // undocumented function gets no Javadoc block.
    EXPECT_EQ(hdr.find("NOT documentation"), std::string::npos) << hdr;
    EXPECT_EQ(hdr.find("*/\ninline auto undocumented("), std::string::npos) << hdr;
}

TEST(LibraryModule, FromImportBindsEnumAndStructBare) {
    // `import Sym[, Sym…] from mod` binds each symbol at file scope, so a consumer names a
    // module's enum member or struct WITHOUT the module prefix — `QuatCol.W`, `Vec({…})` — and it
    // resolves unambiguously to `::cheatah::geo::QuatCol::W` / `::cheatah::geo::Vec`.
    const std::string dir = make_module_dir("fi", "geo");
    const std::string root = dir.substr(0, dir.rfind('/'));
    write_file(dir + "/geo.purr",
               "import ndarray\n"
               "enum QuatCol { X, Y, Z, W }\n"
               "struct Vec { x: int  y: int }\n"
               "fn width() -> int { return 4 }\n");   // a FUNCTION, from-imported alongside the types
    ASSERT_EQ(run(kPurrc + " --emit-library --transparent " + dir + "/geo.purr -o " + dir + "/geo.hpp"), 0);
    const std::string prog = root + "/prog.purr", mod = root + "/prog.so";
    write_file(prog,
               "import io\n"
               "import ndarray\n"
               "import QuatCol, Vec, width from geo\n"  // type, struct, AND function — one statement
               "let q = ndarray.zeros([4])\n"
               "q[QuatCol.W] = 7.0\n"                 // enum member as a bare subscript
               "let v: Vec = Vec({ .x = 3, .y = 4 })\n"  // struct as a bare type AND constructor
               "io.print(q[QuatCol.W], v.x, v.y, width())\n");  // bare function call
    ASSERT_EQ(run("CHEATAH_MODULE_PATH=" + root + " " + kPurrc + " " + prog + " -o " + mod), 0);
    FILE* pipe = popen((std::string(CHEATAH_RUNTIME_PATH) + " " + mod + " 2>/dev/null").c_str(), "r");
    ASSERT_NE(pipe, nullptr);
    std::string out;
    char buf[64];
    while (std::fgets(buf, sizeof buf, pipe)) out += buf;
    pclose(pipe);
    EXPECT_EQ(out, "7 3 4 4\n");
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
