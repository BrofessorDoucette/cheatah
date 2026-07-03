// memory cheatah-surface e2e (suite MemoryCheatah) — runs the REAL cheatah `.purr` programs in
// stdlib/memory/tests/ and checks exact stdout. These are actual cheatah programs exercising the
// memory surface (own / rwrite / rread / acquire / read / write / valid / expired), verified to parse
// + codegen + compile.
//
// The Owner engine is implemented, so these are real GREEN output checks in the gate: purrc builds
// the module + program, cheatah runs it, and stdout must match exactly.

#include <string>

#include "e2e_harness.hpp"

namespace {
void expect_purr(const char* file, const std::string& expected) {
    int rc = -1;
    const std::string path = std::string(MEMORY_PURR_DIR) + "/" + file;
    const std::string out = e2e::run_purr_file(std::string("mem_") + file, path, rc);
    EXPECT_EQ(rc, 0) << file << ": exited non-zero";
    EXPECT_EQ(out, expected) << file << ": stdout mismatch";
}
}  // namespace

TEST(MemoryCheatah, ScalarWriteReadModifyWrite) { expect_purr("cheatah_scalar.purr",      "5\n7\n12\n"); }
TEST(MemoryCheatah, StructPerFieldWrites)       { expect_purr("cheatah_struct.purr",      "0 0\n3 4\n7\n"); }
TEST(MemoryCheatah, AccumulateDeterministic)    { expect_purr("cheatah_accumulate.purr",  "1000\n0\n"); }
TEST(MemoryCheatah, StringOwnerGrows)           { expect_purr("cheatah_string.purr",      "5\nhello world\n11\n"); }
TEST(MemoryCheatah, ReadLeaseValidState)        { expect_purr("cheatah_lease_state.purr", "True\nFalse\n42\n"); }
TEST(MemoryCheatah, OwnerOfNdArrayElements)     { expect_purr("cheatah_ndarray.purr",     "True\nTrue\nTrue\n"); }
TEST(MemoryCheatah, TwoOwnersInvariantPreserved){ expect_purr("cheatah_interleaved.purr", "50 50\n100\n"); }
// Real cheatah CONCURRENCY: N threads (thread.spawn) share one pinned Owner; the deterministic-final
// total proves the engine's drain-before-write exclusion end-to-end through the language.
TEST(MemoryCheatah, ConcurrentSumOverSharedOwner) { expect_purr("cheatah_concurrent_sum.purr", "20000\n"); }
