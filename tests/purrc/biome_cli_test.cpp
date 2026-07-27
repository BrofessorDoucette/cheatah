// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// biome CLI end-to-end tests (suite BiomeCli) — the "Biome Standard" behavior of the
// package manager. pkg-manager/biome.purr is compiled ONCE per suite with purrc, then
// each test runs `cheatah biome.so <args>` inside its own scratch directory under
// PURR_TEST_TMP, so no test depends on another's state.
//
// NOTE on exit codes: biome's run() computes a meaningful status, but the top-level
// `run()` call discards its return value, so the PROCESS exit code is 0 even on
// refusals (a known pre-existing wart). Error-path tests therefore assert on the
// printed stdout text, never on the exit code.
#include "e2e_harness.hpp"

#include <unistd.h>

#include <filesystem>

#ifndef BIOME_PURR_PATH
#define BIOME_PURR_PATH ""
#endif
#ifndef BIOME_STANDARDS_DIR
#define BIOME_STANDARDS_DIR ""
#endif

namespace {

namespace fs = std::filesystem;

class BiomeCli : public ::testing::Test {
 protected:
  // Per-process path: under `ctest -j` each test runs as its own process of this binary,
  // and every process recompiles biome.purr in SetUpTestSuite — a shared path makes those
  // compilations race (one process loads another's half-written .so).
  static std::string module_path() {
    return std::string(PURR_TEST_TMP) + "/biome_cli_" + std::to_string(getpid()) + ".so";
  }

  // Compile pkg-manager/biome.purr once for the whole suite.
  static void SetUpTestSuite() {
    const std::string cmd = std::string(PURRC_PATH) + " \"" + BIOME_PURR_PATH + "\" -o \"" +
                            module_path() + "\"";
    ASSERT_EQ(std::system(cmd.c_str()), 0) << "purrc failed to compile biome.purr";
    ASSERT_TRUE(fs::exists(module_path()));
  }

  // A fresh, empty scratch directory for one test.
  static std::string scratch(const std::string& name) {
    const fs::path dir = fs::path(PURR_TEST_TMP) / "biome_cli" / name;
    fs::remove_all(dir);
    fs::create_directories(dir);
    return dir.string();
  }

  // Run `biome <args>` with `dir` as the working directory; captures stdout+stderr.
  static std::string biome(const std::string& dir, const std::string& args, int& rc) {
    const std::string cmd = "cd \"" + dir + "\" && \"" + std::string(CHEATAH_RUNTIME_PATH) +
                            "\" \"" + module_path() + "\" " + args + " 2>&1";
    return e2e::run_capture(cmd, rc);
  }
  static std::string biome(const std::string& dir, const std::string& args) {
    int rc = -1;
    const std::string out = biome(dir, args, rc);
    EXPECT_EQ(rc, 0) << "biome " << args << " did not run cleanly:\n" << out;
    return out;
  }

  static void expect_has(const std::string& hay, const std::string& needle) {
    EXPECT_NE(hay.find(needle), std::string::npos) << "missing `" << needle << "` in:\n" << hay;
  }
  static void expect_lacks(const std::string& hay, const std::string& needle) {
    EXPECT_EQ(hay.find(needle), std::string::npos) << "unexpected `" << needle << "` in:\n" << hay;
  }

  // Scaffold a project with `biome init proj` inside a fresh scratch dir and return
  // the project directory (<scratch>/proj).
  static std::string init_project(const std::string& test_name) {
    const std::string dir = scratch(test_name);
    const std::string out = biome(dir, "init proj");
    EXPECT_NE(out.find("biome: created project proj"), std::string::npos) << out;
    return dir + "/proj";
  }
};

// `biome standards` lists the known sets: the newest carries the `* ` marker, with its
// members' resolved tags, status and release date. Earlier sets stay listed as
// `supported` — the table is append-only, so a released standard never disappears, and
// asserting BOTH rows is what proves that rather than just the newest.
TEST_F(BiomeCli, StandardsListsKnownSets) {
  const std::string out = biome(scratch("standards_list"), "standards");
  expect_has(out, "* 0.2.0-alpha  (current, released 2026-07-27)");
  expect_has(out, "cheatah         v1.8.0-alpha");   // ljust(name, 16) columns
  expect_has(out, "cheatah-gpu     v0.5.0-alpha");
  expect_has(out, "0.1.0-alpha  (supported, released 2026-07-25)");
  expect_has(out, "cheatah         v1.7.0-alpha");
}

// `--emit-toml <ver>` reproduces the committed standards/*.toml byte-for-byte — the
// drift check between the in-program table and the canonical files.
TEST_F(BiomeCli, StandardsEmitTomlByteIdenticalToCommittedFile) {
  const std::string committed =
      e2e::read_file(std::string(BIOME_STANDARDS_DIR) + "/biome-standard-0.1.0-alpha.toml");
  ASSERT_FALSE(committed.empty()) << "missing standards/biome-standard-0.1.0-alpha.toml";
  const std::string out = biome(scratch("emit_toml"), "standards --emit-toml 0.1.0-alpha");
  EXPECT_EQ(out, committed);
}

TEST_F(BiomeCli, StandardsEmitTomlUnknownVersion) {
  const std::string out = biome(scratch("emit_toml_bad"), "standards --emit-toml 9.9.9");
  expect_has(out, "biome: unknown Biome Standard: 9.9.9");
}

// `biome init` pins the newest standard: cheatah.toml gets `standard = "0.2.0-alpha"`
// under [cheatah] and NO manual `version =` override; the generated CMake pins the
// toolchain tag the standard resolves.
TEST_F(BiomeCli, InitScaffoldsStandardPinnedProject) {
  const std::string proj = init_project("init");
  const std::string toml = e2e::read_file(proj + "/cheatah.toml");
  ASSERT_FALSE(toml.empty());
  expect_has(toml, "[cheatah]");
  expect_has(toml, "standard = \"0.2.0-alpha\"");
  expect_lacks(toml, "\nversion =");  // no manual toolchain override in a fresh project
  const std::string cml = e2e::read_file(proj + "/CMakeLists.txt");
  expect_has(cml, "GIT_TAG v1.8.0-alpha");
  EXPECT_TRUE(fs::exists(proj + "/src/main.purr"));
  EXPECT_TRUE(fs::exists(proj + "/cmake/CPM.cmake"));
}

// Adding a member extension resolves its tag from the standard: manifest value,
// regenerated CMake, and the confirmation message all carry v0.5.0-alpha.
TEST_F(BiomeCli, AddStandardMemberExtension) {
  const std::string proj = init_project("add_gpu");
  const std::string out = biome(proj, "add cheatah-gpu");
  expect_has(out, "biome: added cheatah-gpu v0.5.0-alpha — GPU arrays and compute kernels");
  expect_has(out, "(from Biome Standard 0.2.0-alpha)");
  const std::string toml = e2e::read_file(proj + "/cheatah.toml");
  expect_has(toml, "[extensions]");
  expect_has(toml, "cheatah-gpu = \"v0.5.0-alpha\"");
  const std::string cml = e2e::read_file(proj + "/CMakeLists.txt");
  expect_has(cml,
             "CPMAddPackage(NAME cheatah-gpu GITHUB_REPOSITORY BrofessorDoucette/cheatah-gpu "
             "GIT_TAG v0.5.0-alpha)");
}

// A known extension that is NOT a member of the pinned standard is refused, and
// nothing is written. (Exit code stays 0 — the run() wart — so assert text only.)
TEST_F(BiomeCli, AddNonMemberExtensionRefused) {
  const std::string proj = init_project("add_plot");
  const std::string before = e2e::read_file(proj + "/cheatah.toml");
  const std::string out = biome(proj, "add cheatah-plot");
  expect_has(out, "biome: cheatah-plot is not a member of Biome Standard 0.2.0-alpha");
  EXPECT_EQ(e2e::read_file(proj + "/cheatah.toml"), before) << "refusal must not edit the manifest";
  expect_lacks(e2e::read_file(proj + "/CMakeLists.txt"), "cheatah-plot");
}

TEST_F(BiomeCli, AddUnknownExtensionRefused) {
  const std::string proj = init_project("add_nonsense");
  const std::string out = biome(proj, "add nonsense");
  expect_has(out, "biome: unknown extension: nonsense");
  expect_lacks(e2e::read_file(proj + "/cheatah.toml"), "nonsense");
}

// `biome remove` takes the extension back out of both the manifest and the CMake.
TEST_F(BiomeCli, RemoveExtension) {
  const std::string proj = init_project("remove_gpu");
  biome(proj, "add cheatah-gpu");
  ASSERT_NE(e2e::read_file(proj + "/cheatah.toml").find("cheatah-gpu"), std::string::npos);
  const std::string out = biome(proj, "remove cheatah-gpu");
  expect_has(out, "biome: removed cheatah-gpu");
  expect_lacks(e2e::read_file(proj + "/cheatah.toml"), "cheatah-gpu");
  expect_lacks(e2e::read_file(proj + "/CMakeLists.txt"), "cheatah-gpu");
}

// `biome list` marks added members `* ` with the standard's tag, shows non-members as
// "(not in this standard)", and names the pinned standard in the header.
TEST_F(BiomeCli, ListShowsMembershipAndStandard) {
  const std::string proj = init_project("list");
  biome(proj, "add cheatah-gpu");
  const std::string out = biome(proj, "list");
  expect_has(out, "Biome Standard 0.2.0-alpha");
  expect_has(out, "* cheatah-gpu     v0.5.0-alpha");           // added member: marked + tagged
  expect_has(out, "  cheatah-plot    (not in this standard)");  // known, but untested with this set
  expect_has(out, "  cheatah-space   (not in this standard)");
}

// A hand-edited manual `version =` under [cheatah] overrides the TOOLCHAIN tag only:
// generated CMake pins cheatah at v1.6.0-alpha while extensions still resolve their
// tags from the standard.
TEST_F(BiomeCli, ManualVersionOverridesToolchainTagOnly) {
  const std::string dir = scratch("manual_override");
  std::ofstream(dir + "/cheatah.toml")
      << "[project]\nname = \"proj\"\n\n"
         "[cheatah]\nversion = \"1.6.0-alpha\"\nstandard = \"0.1.0-alpha\"\n\n"
         "[extensions]\n\n[dependencies]\n";
  const std::string out = biome(dir, "add cheatah-gpu");
  expect_has(out, "biome: added cheatah-gpu v0.5.0-alpha");
  const std::string cml = e2e::read_file(dir + "/CMakeLists.txt");
  expect_has(cml,
             "CPMAddPackage(NAME cheatah GITHUB_REPOSITORY BrofessorDoucette/cheatah "
             "GIT_TAG v1.6.0-alpha)");  // the manual override
  expect_has(cml,
             "CPMAddPackage(NAME cheatah-gpu GITHUB_REPOSITORY BrofessorDoucette/cheatah-gpu "
             "GIT_TAG v0.5.0-alpha)");  // still from the standard
  const std::string toml = e2e::read_file(dir + "/cheatah.toml");
  expect_has(toml, "version = \"1.6.0-alpha\"");  // the override survives re-serialization
  expect_has(toml, "standard = \"0.1.0-alpha\"");
}

// A manifest pinning a standard this biome does not know: `add` and `configure` both
// refuse with the version named, and write NOTHING (no manifest edit, no CMakeLists,
// no cmake run). (Process exit code is still 0 — see the run() wart above.)
TEST_F(BiomeCli, UnknownStandardRefusesToWrite) {
  const std::string dir = scratch("unknown_standard");
  const std::string manifest =
      "[project]\nname = \"proj\"\n\n[cheatah]\nstandard = \"9.9.9\"\n\n"
      "[extensions]\n\n[dependencies]\n";
  std::ofstream(dir + "/cheatah.toml") << manifest;

  const std::string add_out = biome(dir, "add cheatah-gpu");
  expect_has(add_out, "biome: unknown Biome Standard: 9.9.9");
  EXPECT_EQ(e2e::read_file(dir + "/cheatah.toml"), manifest) << "add must not edit the manifest";
  EXPECT_FALSE(fs::exists(dir + "/CMakeLists.txt"));

  const std::string cfg_out = biome(dir, "configure");
  expect_has(cfg_out, "biome: unknown Biome Standard: 9.9.9");
  EXPECT_FALSE(fs::exists(dir + "/CMakeLists.txt")) << "configure must not generate on refusal";
  EXPECT_FALSE(fs::exists(dir + "/build")) << "configure must not run cmake on refusal";
}

// A legacy manifest with only `version = "1.6.0-alpha"` (no standard key) parses with
// the default standard (the newest); the next save writes the standard line out.
TEST_F(BiomeCli, LegacyManifestDefaultsStandard) {
  const std::string dir = scratch("legacy_manifest");
  std::ofstream(dir + "/cheatah.toml")
      << "[project]\nname = \"proj\"\n\n[cheatah]\nversion = \"1.6.0-alpha\"\n\n"
         "[extensions]\n\n[dependencies]\n";
  const std::string out = biome(dir, "add cheatah-gpu");  // loads, validates, saves
  expect_has(out, "biome: added cheatah-gpu v0.5.0-alpha");
  const std::string toml = e2e::read_file(dir + "/cheatah.toml");
  expect_has(toml, "standard = \"0.2.0-alpha\"");  // written on the first save
  expect_has(toml, "version = \"1.6.0-alpha\"");   // the legacy pin is preserved
}

}  // namespace
