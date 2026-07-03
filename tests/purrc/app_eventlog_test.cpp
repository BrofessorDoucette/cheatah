// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level "application" test: a small event-log pipeline that only passes
// if datetime + time + os + io + string all cooperate end to end.
//
// Unlike the per-module system tests in stdlib_e2e_test.cpp (which exercise one
// module each), this is a realistic multi-module program:
//
//   1. datetime.format()  turns FIXED epochs (deterministic under TZ=UTC) into
//      timestamps, which io.format() + string.upper() compose into log lines;
//   2. io.open()/File.write() persist the lines to a temp file whose path is
//      built and normalized with os.path.join()/os.path.normpath(), then
//      os.path.isfile() confirms the write landed;
//   3. io.read_file() reads it back, string.splitlines()/len()/string.count()
//      derive a DETERMINISTIC summary (line count, first/last line, error tally);
//   4. time.perf_counter() across a tiny sleep yields a monotonic PROPERTY check
//      printed as a boolean (raw clock values are never printed);
//   5. os.remove() cleans up and os.path.exists() confirms the teardown.
//
// The 4th arg pins TZ=UTC so datetime.format() is deterministic. The expected
// stdout was captured byte-for-byte by compiling+running the program under
// TZ=UTC before being hardcoded here.

#include "e2e_harness.hpp"


TEST(SystemApps, EventLog) {
    e2e::expect_e2e("app_eventlog", R"PURR(import io
import os
import string
import datetime
import time

# Fixed epochs (seconds) -> deterministic timestamps under TZ=UTC.
let epochs = [0.0, 3661.0, 90061.0]
let levels = ["INFO", "WARN", "ERROR"]
let msgs = ["service start", "cache miss", "disk full"]

# Build one normalized temp path with os.path.
let path = os.path.normpath(os.path.join("/tmp", "cheatah_eventlog", "..", "cheatah_eventlog.log"))

# Format each event into "TS [LEVEL] msg" via datetime + io.format + string.
let n = len(epochs)
let f = io.open(path, "w")
let i = 0
while i < n {
    let ts = datetime.format(epochs[i], "%Y-%m-%d %H:%M:%S")
    let line = io.format("{} [{}] {}", ts, string.upper(levels[i]), msgs[i])
    f.write(line)
    f.write("\n")
    i = i + 1
}
f.close()

# os confirms the file the pipeline just produced exists.
let wrote_ok = os.path.isfile(path)

# Read it back with io, split into lines with string.
let blob = io.read_file(path)
let lines = string.splitlines(blob)
let count = len(lines)
let first = lines[0]
let last = lines[count - 1]

# time: deterministic monotonic PROPERTY across a tiny sleep (never print raw).
let t0 = time.perf_counter()
time.sleep(0.005)
let t1 = time.perf_counter()
let monotonic_ok = t1 >= t0

# Deterministic summary.
io.print("file:", os.path.basename(path))
io.print("exists:", wrote_ok)
io.print("lines:", count)
io.print("first:", first)
io.print("last:", last)
io.print("errors:", string.count(blob, "[ERROR]"))
io.print("monotonic:", monotonic_ok)

# Clean up the temp file.
os.remove(path)
io.print("cleaned:", not os.path.exists(path))
)PURR",
               "file: cheatah_eventlog.log\n"
               "exists: True\n"
               "lines: 3\n"
               "first: 1970-01-01 00:00:00 [INFO] service start\n"
               "last: 1970-01-02 01:01:01 [ERROR] disk full\n"
               "errors: 1\n"
               "monotonic: True\n"
               "cleaned: True\n",
               "TZ=UTC ");
}
