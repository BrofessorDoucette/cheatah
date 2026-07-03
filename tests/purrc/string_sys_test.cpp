// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// System-level test for the stdlib `string` module: one cohesive text-processing
// pipeline that exercises EVERY public function in stdlib/string/string.hpp in a
// single program (not isolated per-function prints — that is what the compile-run
// suite string_cr_test.cpp does).
//
// The program takes a messy, multi-line "inventory note", then:
//   - normalizes it      (splitlines, strip/lstrip/rstrip)
//   - re-cases it        (upper, lower, capitalize, title, swapcase, capwords)
//   - probes it          (startswith, endswith, contains, find, rfind, count)
//   - tokenizes it       (split-on-sep, split-on-whitespace, join, replace)
//   - classifies tokens  (isdigit, isalpha, isalnum, isupper, islower, isspace)
//   - formats a table    (ljust, rjust, center, zfill)
//
// Every function in the header appears: case ops (upper/lower/capitalize/title/
// swapcase), trimming (strip/lstrip/rstrip), search-test (startswith/endswith/
// contains/find/rfind/count), transform (replace/split[2 overloads]/splitlines/
// capwords/join), padding (ljust/rjust/center/zfill), classifiers (isdigit/
// isalpha/isalnum/isspace/isupper/islower).
//
// List-returning split/splitlines are join()ed before printing; booleans go
// through io.str (print as True/False). Fully DETERMINISTIC — stdout is asserted
// byte-for-byte. Note: purrc treats a newline as a statement terminator, so each
// call stays on one source line and wide rows are built with `+` concatenation;
// only \n \t \" \\ escapes are supported by the lexer (no \r).
#include "e2e_harness.hpp"

TEST(StdlibE2E, String) {
    e2e::expect_e2e("string_sys", R"PURR(import io
import string

let raw = "  RaW inVENTORY  \nsku001, sku002 , Cat-Food\nQTY:  42  units\n"

# ---------------- normalize ----------------
io.print(string.center(" REPORT ", 36, "="))

# splitlines breaks the raw note into logical lines
let lines = string.splitlines(raw)
io.print(string.join(" / ", lines))

# strip / lstrip / rstrip clean the first line's edges
let header = lines[0]
io.print("strip   :" + string.strip(header))
io.print("lstrip  :" + string.lstrip(header))
io.print("rstrip  :" + string.rstrip(header) + "|")

# ---------------- case ops ----------------
let clean = string.strip(header)
io.print("upper   :" + string.upper(clean))
io.print("lower   :" + string.lower(clean))
io.print("cap     :" + string.capitalize(clean))
io.print("title   :" + string.title(clean))
io.print("swap    :" + string.swapcase(clean))
io.print("capwords:" + string.capwords(raw))

# ---------------- search / test ----------------
let line2 = string.strip(lines[1])
io.print("starts  :" + io.str(string.startswith(line2, "sku")))
io.print("ends    :" + io.str(string.endswith(line2, "Food")))
io.print("has     :" + io.str(string.contains(line2, "Cat")))
io.print("find    :" + io.str(string.find(line2, "sku")))
io.print("rfind   :" + io.str(string.rfind(line2, "sku")))
io.print("count   :" + io.str(string.count(line2, "sku")))

# ---------------- transform: split on comma ----------------
let parts = string.split(line2, ",")
io.print("nparts  :" + io.str(len(parts)))
io.print(string.ljust("", 36, "-"))

# clean each comma-part, then split each on whitespace (ws split)
let i = 0
while i < len(parts) {
    let tok = string.strip(parts[i])
    let words = string.split(tok)
    let joined = string.join("_", words)
    let norm = string.replace(joined, "-", "")
    # classify the normalized token
    let kind = "mixed"
    if string.isdigit(norm) {
        kind = "digit"
    }
    if string.isalpha(norm) {
        kind = "alpha"
    }
    if string.isalnum(norm) {
        if kind == "mixed" {
            kind = "alnum"
        }
    }
    let col = string.ljust(string.lower(norm), 12)
    io.print(col + string.rjust(kind, 8))
    i = i + 1
}
io.print(string.ljust("", 36, "-"))

# ---------------- numeric field: line 3 "QTY: 42 units" ----------------
let line3 = lines[2]
let qparts = string.split(line3)
let qty = qparts[1]
io.print("qty.isdigit :" + io.str(string.isdigit(qty)))
io.print("qty.zfill   :" + string.zfill(qty, 6))
io.print("qty.center  :" + string.center(qty, 8, "."))

# ---------------- classifier sweep ----------------
io.print("upper? ABC  :" + io.str(string.isupper("ABC")))
io.print("lower? abc  :" + io.str(string.islower("abc")))
io.print("space?      :" + io.str(string.isspace("  \t")))

io.print(string.center(" END ", 36, "="))
)PURR",
        "============== REPORT ==============\n"
        "  RaW inVENTORY   / sku001, sku002 , Cat-Food / QTY:  42  units\n"
        "strip   :RaW inVENTORY\n"
        "lstrip  :RaW inVENTORY  \n"
        "rstrip  :  RaW inVENTORY|\n"
        "upper   :RAW INVENTORY\n"
        "lower   :raw inventory\n"
        "cap     :Raw inventory\n"
        "title   :Raw Inventory\n"
        "swap    :rAw INventory\n"
        "capwords:Raw Inventory Sku001, Sku002 , Cat-food Qty: 42 Units\n"
        "starts  :True\n"
        "ends    :True\n"
        "has     :True\n"
        "find    :0\n"
        "rfind   :8\n"
        "count   :2\n"
        "nparts  :3\n"
        "------------------------------------\n"
        "sku001         alnum\n"
        "sku002         alnum\n"
        "catfood        alpha\n"
        "------------------------------------\n"
        "qty.isdigit :True\n"
        "qty.zfill   :000042\n"
        "qty.center  :...42...\n"
        "upper? ABC  :True\n"
        "lower? abc  :True\n"
        "space?      :True\n"
        "=============== END ================\n");
}
