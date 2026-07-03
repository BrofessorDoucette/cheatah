// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level "real app" test: a multi-module grade-report program that only
// passes if io + statistics + string + builtins + math all cooperate end to end.
//
// Unlike the per-module compile-run suites (which exercise one function each),
// this is a small but genuine mini-app: it owns a fixed dataset of student
// scores, classifies each score pass/fail against a threshold and into a letter
// grade, builds an aligned table, and prints a summary computed from the data.
// The program is fully DETERMINISTIC (no clocks/PIDs/RNG), so its stdout is
// asserted byte-for-byte.
//
// Modules exercised together:
//   - io         : print, str, format
//   - statistics : mean, median, stdev
//   - string     : center, ljust, rjust
//   - builtins   : len, float (to_float)
//   - math       : round, min, max
//
// Note: purrc treats a newline as a statement terminator, so every call stays
// on a single source line; wide rows are assembled via `+` concatenation.
#include "e2e_harness.hpp"

TEST(SystemApps, GradeReport) {
    e2e::expect_e2e("app_grades", R"PURR(import io
import string
import math
import statistics
import builtins

# --- classify a numeric score into a letter grade ---
fn letter(score) {
    if score >= 90.0 { return "A" }
    if score >= 80.0 { return "B" }
    if score >= 70.0 { return "C" }
    if score >= 60.0 { return "D" }
    return "F"
}

# --- fixed, deterministic dataset ---
let names = ["Ada", "Bjarne", "Cy", "Dennis", "Edsger"]
let scores = [91.5, 67.0, 100.0, 82.25, 58.5]
let pass_mark = 70.0

# ---------------- header ----------------
io.print(string.center(" GRADE REPORT ", 40, "="))
let head = string.ljust("NAME", 10) + "  " + string.rjust("SCORE", 7) + "  " + string.rjust("GRADE", 6) + "  STATUS"
io.print(head)
io.print(string.ljust("", 40, "-"))

# ---------------- per-student table rows ----------------
let passed = 0
let hi = scores[0]
let lo = scores[0]
let i = 0
while i < len(names) {
    let nm = names[i]
    let sc = scores[i]
    hi = math.max(hi, sc)
    lo = math.min(lo, sc)
    let status = "FAIL"
    if sc >= pass_mark {
        status = "PASS"
        passed = passed + 1
    }
    let col_name = string.ljust(nm, 10)
    let col_score = string.rjust(io.str(math.round(sc)), 7)
    let col_grade = string.rjust(letter(sc), 6)
    io.print(col_name + "  " + col_score + "  " + col_grade + "  " + status)
    i = i + 1
}

# ---------------- summary ----------------
io.print(string.ljust("", 40, "-"))
let n = len(scores)
let rate = math.round(100.0 * float(passed) / float(n))
let stdev2 = math.round(statistics.stdev(scores) * 100.0) / 100.0
io.print(io.format("students : {}", n))
io.print(io.format("passed   : {}/{}  ({} pct)", passed, n, rate))
io.print(io.format("mean     : {}", statistics.mean(scores)))
io.print(io.format("median   : {}", statistics.median(scores)))
io.print(io.format("stdev    : {}", stdev2))
io.print(io.format("range    : {} .. {}", math.round(lo), math.round(hi)))
io.print(string.center(" END ", 40, "="))
)PURR",
        "============= GRADE REPORT =============\n"
        "NAME          SCORE   GRADE  STATUS\n"
        "----------------------------------------\n"
        "Ada              92       A  PASS\n"
        "Bjarne           67       D  FAIL\n"
        "Cy              100       A  PASS\n"
        "Dennis           82       B  PASS\n"
        "Edsger           59       F  FAIL\n"
        "----------------------------------------\n"
        "students : 5\n"
        "passed   : 3/5  (60 pct)\n"
        "mean     : 79.85\n"
        "median   : 82.25\n"
        "stdev    : 17.09\n"
        "range    : 59 .. 100\n"
        "================= END ==================\n");
}
