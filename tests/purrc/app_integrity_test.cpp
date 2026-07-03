// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level "application" test: a small file-integrity / dedup tool written
// in cheatah, compiled with purrc and run under the runtime. Unlike the
// single-module e2e tests in stdlib_e2e_test.cpp, this program only produces the
// expected output if FIVE stdlib modules cooperate end to end:
//
//   - os       — os.path.join builds the temp paths, os.makedirs/os.remove/
//                os.rmdir manage the scratch dir, os.path.getsize/os.path.exists
//                report on the written files.
//   - io       — io.open + File.write writes each file, io.read_file reads it
//                back, io.str renders integers, io.print emits the report.
//   - hashlib  — hashlib.sha256 fingerprints each file's bytes.
//   - string   — string.upper formats the per-file labels.
//   - builtins — len() drives the loops, and string indexing (digest[i]) slices
//                the hex prefix character by character (the language has no slice
//                syntax), plus boolean comparison for the cleanup check.
//
// The pipeline: write three fixed files (file2 is a byte-for-byte duplicate of
// file1), hash each, then detect duplicates by comparing the 64-char hex digests.
// Output is fully deterministic — the sha256 prefixes are the real digests of
// the fixed contents (verified against sha256sum):
//   sha256("the quick brown fox\n") = 6e459fed18dd...
//   sha256("lazy dog sleeps\n")     = 86f4c766f75c...
// The scratch directory is cleaned up and the final line asserts it is gone.

#include "e2e_harness.hpp"

TEST(SystemApps, Integrity) {
    e2e::expect_e2e("app_integrity", R"PURR(# app_integrity.purr — a file integrity / dedup pipeline.
# Exercises hashlib + io + string + os + builtins together.

import io
import os
import string
import hashlib

let dir = os.path.join("/tmp", "cheatah_integrity")
os.makedirs(dir)

# First 12 hex chars of a digest, built one char at a time (the language has no
# slice syntax, so index char-by-char via builtins-backed string indexing).
fn prefix12(digest) {
    let p = ""
    for i in range(0, 12) {
        p = p + digest[i]
    }
    return p
}

# Fixed contents: file2 deliberately duplicates file1.
let names = ["file1.txt", "file2.txt", "file3.txt"]
let bodies = ["the quick brown fox\n", "the quick brown fox\n", "lazy dog sleeps\n"]
let digests = ["", "", ""]

# Write each file via io.open / File.write, then read it back and hash it.
for i in range(0, len(names)) {
    let path = os.path.join(dir, names[i])
    let f = io.open(path, "w")
    f.write(bodies[i])
    f.close()

    let data = io.read_file(path)
    let d = hashlib.sha256(data)
    digests[i] = d
    io.print(string.upper("file") + io.str(i + 1) + ":", os.path.getsize(path), "bytes", prefix12(d))
}

# Dedup: for each file, find the first earlier file with an identical digest.
for i in range(0, len(digests)) {
    let dup = -1
    for j in range(0, i) {
        if dup < 0 {
            if digests[i] == digests[j] {
                dup = j
            }
        }
    }
    if dup >= 0 {
        io.print("dup: file" + io.str(i + 1) + " == file" + io.str(dup + 1))
    } else {
        io.print("unique: file" + io.str(i + 1))
    }
}

# Clean up the temp files and directory.
for i in range(0, len(names)) {
    os.remove(os.path.join(dir, names[i]))
}
os.rmdir(dir)
io.print("cleaned:", os.path.exists(dir) == false)
)PURR",
               "FILE1: 20 bytes 6e459fed18dd\n"
               "FILE2: 20 bytes 6e459fed18dd\n"
               "FILE3: 16 bytes 86f4c766f75c\n"
               "unique: file1\n"
               "dup: file2 == file1\n"
               "unique: file3\n"
               "cleaned: True\n");
}
