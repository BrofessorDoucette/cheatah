// Folded sibling C++ (foo.purr + foo.hpp / foo.cpp): purrc weaves a hand-written C++ base into a
// module's generated output, inside `namespace cheatah::<m>`, so the .purr and the C++ are one
// module — the .purr calls the C++ by bare name, no import bridge. See docs and codegen.cpp.

#include <sys/stat.h>

#include <cstdio>
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
std::string capture(const std::string& cmd) {
    FILE* pipe = popen((cmd + " 2>/dev/null").c_str(), "r");
    if (!pipe) return "";
    std::string out;
    char buf[256];
    while (std::fgets(buf, sizeof buf, pipe)) out += buf;
    pclose(pipe);
    return out;
}
// A fresh per-test package dir <tmp>/folded_<tag>/<name>/ with <name>.purr in it.
std::string make_dir(const std::string& tag, const std::string& name) {
    const std::string root = std::string(PURR_TEST_TMP) + "/folded_" + tag;
    const std::string dir = root + "/" + name;
    run("rm -rf " + root + " && mkdir -p " + dir);
    return dir;
}
const std::string kPurrc = PURRC_PATH;

// A hand-written base header fragment: a naked C++ helper the .purr will call. Returns an `n`-byte
// little-endian string so the .purr can observe its length.
const char* kBaseHpp =
    "// A hand-written C++ fragment folded into the module. No namespace, no bridge.\n"
    "#include <string>\n"
    "[[nodiscard]] inline std::string le2(long long n) {\n"
    "    return std::string{ static_cast<char>(n & 0xFF), static_cast<char>((n >> 8) & 0xFF) };\n"
    "}\n";
}  // namespace

// Case 1 — the proof: auto-detected sibling folded, the .purr calls it, and it runs end to end.
TEST(FoldedBase, SiblingFoldedAndCalledEndToEnd) {
    const std::string dir = make_dir("call", "pk");
    const std::string base = dir + "/pk.hpp";
    write_file(base, kBaseHpp);
    write_file(dir + "/pk.purr", "fn width(n: int) -> int {\n    return len(le2(n))\n}\n");
    const std::string base_before = read_file(base);

    // No flags: the same-stem pk.hpp is auto-detected and folded.
    ASSERT_EQ(run(kPurrc + " --emit-library --transparent " + dir + "/pk.purr --import-root " + dir), 0);

    // The generated output is pk.gen.hpp; the hand-written pk.hpp is left byte-for-byte untouched.
    EXPECT_TRUE(exists(dir + "/pk.gen.hpp"));
    EXPECT_FALSE(exists(dir + "/pk.hpp.gen.hpp"));
    EXPECT_EQ(read_file(base), base_before);

    const std::string hdr = read_file(dir + "/pk.gen.hpp");
    const std::size_t inc = hdr.find("#include <string>");
    const std::size_t ns = hdr.find("namespace cheatah::pk");
    const std::size_t def = hdr.find("inline std::string le2");
    const std::size_t fn = hdr.find("width");  // the transpiled function
    ASSERT_NE(inc, std::string::npos);
    ASSERT_NE(ns, std::string::npos);
    ASSERT_NE(def, std::string::npos);
    ASSERT_NE(fn, std::string::npos);
    EXPECT_LT(inc, ns);   // the #include is hoisted ABOVE the namespace (can't live inside one)
    EXPECT_LT(ns, def);   // the base body is INSIDE the namespace
    EXPECT_LT(def, fn);   // ...and BEFORE the transpiled function, so the .purr can call it

    // A consumer imports the module, calls the .purr fn (which calls the folded C++), and prints 2.
    const std::string prog = dir + "/../use.purr", mod = dir + "/../use.so";
    write_file(prog, "import io\nimport pk\nio.print(pk.width(258))\n");
    ASSERT_EQ(run(kPurrc + " " + prog + " -o " + mod + " --import-root " + dir), 0);
    EXPECT_EQ(capture(std::string(CHEATAH_RUNTIME_PATH) + " " + mod), "2\n");
}

// Case 2 — the off-switch: --no-adjacent ignores the sibling; output is a plain .hpp with no fold.
TEST(FoldedBase, NoAdjacentDisablesTheMagic) {
    const std::string dir = make_dir("off", "pk");
    write_file(dir + "/pk.hpp", kBaseHpp);
    write_file(dir + "/pk.purr", "fn width(n: int) -> int {\n    return len(le2(n))\n}\n");

    ASSERT_EQ(run(kPurrc + " --emit-library --transparent --no-adjacent " + dir + "/pk.purr -o " +
                  dir + "/out.hpp --import-root " + dir), 0);
    const std::string hdr = read_file(dir + "/out.hpp");
    EXPECT_EQ(hdr.find("inline std::string le2"), std::string::npos);  // the DEFINITION was not folded
    EXPECT_NE(hdr.find("le2("), std::string::npos);                    // the call remains (debug view)
    EXPECT_FALSE(exists(dir + "/pk.gen.hpp"));
}

// Case 3 — the safety guard: a sibling that is itself a purrc-generated file is never folded.
TEST(FoldedBase, GeneratedSiblingIsSkipped) {
    const std::string dir = make_dir("guard", "th");
    write_file(dir + "/th.hpp",
               "// Generated by purrc — do not edit.\ninline int stale() { return 9; }\n");
    write_file(dir + "/th.purr", "fn f() -> int {\n    return 1\n}\n");

    ASSERT_EQ(run(kPurrc + " --emit-library --transparent " + dir + "/th.purr -o " + dir +
                  "/th.gen.hpp --import-root " + dir), 0);
    // No fold happened, so no `stale` in the output (and the auto-detect left th.hpp alone).
    EXPECT_EQ(read_file(dir + "/th.gen.hpp").find("stale"), std::string::npos);
}

// Case 4 — program mode: a sibling foo.cpp folds into the program TU and the program runs.
TEST(FoldedBase, ProgramModeSourceFold) {
    const std::string dir = make_dir("prog", "app");
    write_file(dir + "/app.cpp",
               "#include <string>\ninline long long triple(long long n) { return n * 3; }\n");
    write_file(dir + "/app.purr", "import io\nio.print(triple(7))\n");

    const std::string mod = dir + "/app.so";
    ASSERT_EQ(run(kPurrc + " " + dir + "/app.purr -o " + mod + " --import-root " + dir), 0);
    EXPECT_NE(read_file(mod + ".gen.cpp").find("inline long long triple"), std::string::npos);
    EXPECT_EQ(capture(std::string(CHEATAH_RUNTIME_PATH) + " " + mod), "21\n");
}

// Case 5 — the fixed gap: a top-level `cpp{}` in a library module now survives into the header.
TEST(FoldedBase, CppBlockSurvivesInLibraryHeader) {
    const std::string dir = make_dir("cpp", "gr");
    write_file(dir + "/gr.purr",
               "cpp {\n#include <string>\ninline std::string bang(const std::string& s) { return s + \"!\"; }\n}\n\n"
               "fn shout(s: str) -> str {\n    return bang(s)\n}\n");
    ASSERT_EQ(run(kPurrc + " --emit-library --transparent " + dir + "/gr.purr -o " + dir +
                  "/gr.hpp --import-root " + dir), 0);
    const std::string hdr = read_file(dir + "/gr.hpp");
    EXPECT_NE(hdr.find("inline std::string bang"), std::string::npos);  // the cpp{} body is emitted
    EXPECT_LT(hdr.find("inline std::string bang"), hdr.find("namespace cheatah::gr"));  // at file scope
}
