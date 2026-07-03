// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// End-to-end tests for the newer language features: break/continue, elif, match,
// growable lists (append) + dict mutation, method-call syntax, and string/list
// slicing with negative indices. Each writes a .purr, compiles it with purrc,
// runs it under the cheatah runtime, and asserts the exact stdout.
#include "e2e_harness.hpp"

TEST(LangFeatures, BreakContinue) {
    e2e::expect_e2e("lang_break_continue", R"PURR(import io
let sum = 0
for i in range(1, 20) {
    if i == 5 { continue }
    if i == 10 { break }
    sum = sum + i
}
io.print(sum)
)PURR",
                    "40\n");  // 1+2+3+4+6+7+8+9
}

TEST(LangFeatures, ElifChain) {
    e2e::expect_e2e("lang_elif", R"PURR(import io
fn classify(n) {
    if n < 0 { return "neg" }
    elif n == 0 { return "zero" }
    elif n < 10 { return "small" }
    else { return "big" }
}
for n in [-3, 0, 7, 99] {
    io.print(classify(n))
}
)PURR",
                    "neg\nzero\nsmall\nbig\n");
}

TEST(LangFeatures, Match) {
    e2e::expect_e2e("lang_match", R"PURR(import io
for x in [1, 2, 3] {
    match x {
        case 1 { io.print("one") }
        case 2 { io.print("two") }
        case _ { io.print("many") }
    }
}
)PURR",
                    "one\ntwo\nmany\n");
}

TEST(LangFeatures, CompoundAssignment) {
    // += -= *= /= on ints, floats, and strings (lowers to the C++ operators).
    e2e::expect_e2e("lang_compound_assign", R"PURR(import io
let n = 10
n += 5
n -= 3
n *= 4
let x = 9.0
x /= 2.0
let s = "purr"
s += "fect"
io.print(n, x, s)
)PURR", "48 4.5 purrfect\n");
}

TEST(LangFeatures, NdarrayOperators) {
    // Infix elementwise/scalar arithmetic and in-place compound assignment on
    // ndarrays: a * 2.0, a + b, a += b, a /= scalar.
    e2e::expect_e2e("lang_ndarray_operators", R"PURR(import io
import ndarray
let a = ndarray.array([1.0, 2.0, 3.0])
let b = ndarray.array([10.0, 20.0, 30.0])
io.print(ndarray.to_string(a * 2.0))
io.print(ndarray.to_string(0.5 * b))
io.print(ndarray.to_string(a + b))
a += b
a /= 11.0
io.print(ndarray.to_string(a))
)PURR", "[2, 4, 6]\n[5, 10, 15]\n[11, 22, 33]\n[1, 2, 3]\n");
}

TEST(LangFeatures, ParamsPassByReference) {
    // Parameters bind by reference (Python object semantics): a function that
    // mutates a struct field, an ndarray element, or appends to a list through
    // its parameter changes the CALLER'S object. A literal argument still works
    // (binds as a temporary).
    e2e::expect_e2e("lang_byref_params", R"PURR(import io
import ndarray

struct Counter { hits: int }

fn bump(c: Counter) {
    c.hits += 1
}

fn scale_in_place(a, factor: float) {
    a *= factor
}

fn push_two(xs: list) {
    xs.append(2)
}

let c = Counter(0)
bump(c)
bump(c)
let a = ndarray.array([1.0, 2.0])
scale_in_place(a, 10.0)
let xs = [1]
push_two(xs)
io.print(c.hits, ndarray.to_string(a), xs)
)PURR", "2 [10, 20] [1, 2]\n");
}

TEST(LangFeatures, NdarraySubscript) {
    // x[i] and x[i, j] subscripts on ndarrays: reads, writes, negatives, and the
    // typed `: ndarray<int>` parameter spelling (in-place updates reach the caller).
    e2e::expect_e2e("lang_ndarray_subscript", R"PURR(import io
import ndarray

fn light_pixel(row : ndarray<int>) {
    row[0] = 1
}

let grid = ndarray.reshape(ndarray.array([1.0, 2.0, 3.0, 4.0]), [2, 2])
grid[1, 0] = 9.0
io.print(grid[1, 0], grid[0, 1], grid[-1, -1])
let row = ndarray.array([0, 0, 0])
light_pixel(row)
row[2] = 5
io.print(ndarray.to_string(row))
)PURR", "9 2 4\n[1, 0, 5]\n");
}

TEST(LangFeatures, MatchWildcardWithAccumulator) {
    // Regression: a `case _` wildcard has no pattern expression; the dead-let
    // analysis scanning a match for reads of `acc` must not dereference it.
    e2e::expect_e2e("lang_match_wildcard_acc", R"PURR(import io
fn grade(n: int) {
    let acc = 0.0
    match n {
        case 1 {
            acc += 1.0
        }
        case _ {
        }
    }
    return acc
}
io.print(grade(1), grade(7))
)PURR", "1 0\n");
}

TEST(LangFeatures, AppendAndDictMutation) {
    e2e::expect_e2e("lang_append", R"PURR(import io
let xs: list<int> = []
append(xs, 1)
xs.append(2)
xs.append(3)
io.print(len(xs), xs[2])
let counts: dict<str, int> = {}
counts["x"] = 1
counts["x"] = counts["x"] + 5
io.print(counts["x"])
)PURR",
                    "3 3\n6\n");
}

TEST(LangFeatures, MethodPredicates) {
    e2e::expect_e2e("lang_methods", R"PURR(import io
io.print("</div>".startswith("</"))
io.print("hello".endswith("lo"))
io.print("abcd".contains("bc"))
)PURR",
                    "True\nTrue\nTrue\n");
}

TEST(LangFeatures, StringSlicingAndIndex) {
    e2e::expect_e2e("lang_slice_str", R"PURR(import io
let s = "hello world"
io.print(s[0], s[-1])
io.print(s[0:5])
io.print(s[6:])
io.print(s[:5])
io.print(s[-5:])
io.print(s[0] == "h")
)PURR",
                    "h d\nhello\nworld\nhello\nworld\nTrue\n");
}

TEST(LangFeatures, ListSlicingAndIndex) {
    e2e::expect_e2e("lang_slice_list", R"PURR(import io
let nums = [10, 20, 30, 40, 50]
io.print(nums[-1], nums[1])
let mid = nums[1:4]
io.print(len(mid), mid[0], mid[2])
)PURR",
                    "50 20\n3 20 40\n");
}

TEST(LangFeatures, ReturnTypeHints) {
    // Optional Python-style `-> Type` return hints. When present the function lowers with
    // that concrete C++ return type (the backend enforces it); when absent the return stays
    // `auto`. Mixed here: int, float, an ndarray, and an untyped function all interoperate.
    e2e::expect_e2e("lang_return_hints", R"PURR(import io
import ndarray
fn add(a : int, b : int) -> int {
    return a + b
}
fn half(x : float) -> float {
    return x / 2.0
}
fn untyped(x) {
    return x + 1
}
fn ones(n : int) -> ndarray<float> {
    let a = ndarray.zeros([n])
    for i in range(0, n) {
        a[i] = 1.0
    }
    return a
}
io.print(add(2, 3))
io.print(half(9.0))
io.print(untyped(41))
io.print(ndarray.to_string(ones(3)))
)PURR",
                    "5\n4.5\n42\n[1, 1, 1]\n");
}

// Explicit template arguments on a call/construction, including NON-TYPE (integer-literal)
// args: `f<3>(x)` and the mixed `f<int, 4>(x)`. A top-level cpp{} block supplies the C++
// template fixtures so the program compiles AND runs; we also assert the emitted C++ carries
// the literal (and the mapped type) verbatim. Guards the ambiguity with `<` as a comparison:
// the args only commit when the angle list closes and is immediately followed by `(`.
TEST(LangFeatures, NonTypeTemplateArgs) {
    const std::string gen = e2e::expect_e2e_source("lang_nontype_targs", R"PURR(import io
cpp {
template <long long N>
long long times_n(long long x) { return x * N; }

template <typename T, long long N>
T scale_n(T x) { return x * static_cast<T>(N); }
}
io.print(times_n<3>(7))
io.print(scale_n<int, 4>(5))
)PURR",
                                                  "21\n20\n");
    // The single non-type arg is spliced verbatim.
    EXPECT_NE(gen.find("times_n<3>"), std::string::npos)
        << "expected the non-type template arg `<3>` in the emitted C++";
    // Mixed: the type arg maps (int -> long long) and the non-type literal passes through.
    EXPECT_NE(gen.find("scale_n<long long, 4>"), std::string::npos)
        << "expected mixed `<long long, 4>` type + non-type args in the emitted C++";
}

// Compile-time `if constexpr (cond) {…}` — kept C++-style (parenthesised condition) and
// lowered verbatim to C++ `if constexpr`, so the live branch is chosen at COMPILE time from
// a constant condition. The constexpr-ness threads down the `else if constexpr` chain. A
// cpp{} block supplies the constexpr fixtures so the program compiles AND runs; we also
// assert the emitted C++ carries `if constexpr (` on both arms. Boolean logic (`and`,
// comparisons) in the condition goes through the ordinary expression path.
TEST(LangFeatures, IfConstexpr) {
    const std::string gen = e2e::expect_e2e_source("lang_if_constexpr", R"PURR(import io
cpp {
constexpr int kMode = 2;
constexpr bool kOn = true;
}
fn pick() {
    if constexpr (kMode == 1 and kOn) {
        return "one"
    } else if constexpr (kMode == 2) {
        return "two"
    } else {
        return "other"
    }
}
io.print(pick())
)PURR",
                                                  "two\n");
    // The leading `if` lowers to a compile-time branch...
    const std::size_t first = gen.find("if constexpr (");
    ASSERT_NE(first, std::string::npos)
        << "expected `if constexpr (` in the emitted C++";
    // ...and the `else if constexpr` arm inherits it (a second occurrence).
    EXPECT_NE(gen.find("if constexpr (", first + 1), std::string::npos)
        << "the else-if arm should also lower to `if constexpr`";
}

// ============================================================================
// Compile-time `constexpr` family — `constexpr let` / `constexpr fn` /
// `constexpr match`, plus the AUTO-promotion of `if`/`match` over a known
// constant, and the `match` -> `switch` vs `if/else-if` smart lowering.
// These exercise the cases most likely to break the transpiler as it grows.
// ============================================================================

// `constexpr let` emits a C++ `constexpr` binding AND marks the name a compile-time
// constant, so a plain `if` over it AUTO-lowers to `if constexpr`.
TEST(LangFeatures, ConstexprLetAutoIf) {
    const std::string gen = e2e::expect_e2e_source("lang_cx_let_autoif", R"PURR(import io
constexpr let N = 4
if (N == 4) {
    io.print("four")
} else {
    io.print("other")
}
)PURR",
                                                   "four\n");
    EXPECT_NE(gen.find("constexpr auto N"), std::string::npos) << "let must emit `constexpr`";
    EXPECT_NE(gen.find("if constexpr ("), std::string::npos)
        << "an `if` over a constexpr let must auto-lower to `if constexpr`";
}

// Constant-folding initializer + reference to an earlier constexpr let (chained constants).
TEST(LangFeatures, ConstexprLetArithmeticAndChain) {
    const std::string gen = e2e::expect_e2e_source("lang_cx_let_chain", R"PURR(import io
constexpr let A = 2 * 3 + 1
constexpr let B = A + 5
if (B == 12) { io.print("ok") } else { io.print("bad") }
)PURR",
                                                   "ok\n");
    EXPECT_NE(gen.find("constexpr auto A"), std::string::npos);
    EXPECT_NE(gen.find("constexpr auto B"), std::string::npos);
    EXPECT_NE(gen.find("if constexpr ("), std::string::npos);
}

// Explicit type annotation on a constexpr let still emits `constexpr`.
TEST(LangFeatures, ConstexprLetTyped) {
    const std::string gen = e2e::expect_e2e_source("lang_cx_let_typed", R"PURR(import io
constexpr let x: int = 5
if (x < 10) { io.print("small") } else { io.print("big") }
)PURR",
                                                   "small\n");
    EXPECT_NE(gen.find("constexpr "), std::string::npos) << "typed constexpr let must emit `constexpr`";
    EXPECT_NE(gen.find("if constexpr ("), std::string::npos);
}

// Bool constexpr let drives an `if constexpr` with an `else` arm.
TEST(LangFeatures, ConstexprLetBool) {
    e2e::expect_e2e("lang_cx_let_bool", R"PURR(import io
constexpr let on = true
if (on) { io.print("on") } else { io.print("off") }
)PURR",
                    "on\n");
}

// A purely-literal condition is itself a constant -> auto `if constexpr`, no `let` needed.
TEST(LangFeatures, AutoIfLiteralCondition) {
    const std::string gen = e2e::expect_e2e_source("lang_cx_literal_if", R"PURR(import io
if (1 + 1 == 2) { io.print("math") } else { io.print("broken") }
)PURR",
                                                   "math\n");
    EXPECT_NE(gen.find("if constexpr ("), std::string::npos);
}

// GUARD: a condition over a RUNTIME value (a function parameter) must stay a runtime `if`
// — never auto-promoted (which would fail to compile, the value isn't constexpr).
TEST(LangFeatures, RuntimeIfNotPromoted) {
    const std::string gen = e2e::expect_e2e_source("lang_cx_runtime_if", R"PURR(import io
fn label(x: int) {
    if (x == 1) { return "one" } else { return "many" }
}
io.print(label(1), label(9))
)PURR",
                                                   "one many\n");
    EXPECT_EQ(gen.find("if constexpr"), std::string::npos)
        << "an `if` over a runtime value must NOT be promoted to `if constexpr`";
}

// `constexpr fn`: a constexpr function whose call folds inside a `constexpr let`, and which
// is ALSO callable at runtime.
TEST(LangFeatures, ConstexprFn) {
    const std::string gen = e2e::expect_e2e_source("lang_cx_fn", R"PURR(import io
constexpr fn square(x) { return x * x }
constexpr let r = square(5)
if (r == 25) { io.print("r25") } else { io.print("no") }
io.print(square(3))
)PURR",
                                                   "r25\n9\n");
    EXPECT_NE(gen.find("constexpr auto square"), std::string::npos)
        << "constexpr fn must emit a `constexpr` function";
    EXPECT_NE(gen.find("if constexpr ("), std::string::npos)
        << "an `if` over the constexpr-folded result must auto-lower";
}

// Explicit `constexpr match` over a constant subject -> compile-time `if constexpr` chain.
TEST(LangFeatures, ConstexprMatchExplicit) {
    const std::string gen = e2e::expect_e2e_source("lang_cx_match_explicit", R"PURR(import io
constexpr let k = 2
constexpr match k {
    case 1 { io.print("one") }
    case 2 { io.print("two") }
    case _ { io.print("other") }
}
)PURR",
                                                   "two\n");
    EXPECT_NE(gen.find("if constexpr ("), std::string::npos)
        << "constexpr match must lower to an `if constexpr` chain";
    EXPECT_EQ(gen.find("switch ("), std::string::npos)
        << "constexpr match must NOT lower to a runtime switch";
}

// AUTO: a plain `match` over a constant subject with constant case labels also folds to the
// compile-time `if constexpr` chain (no `constexpr` keyword on the match needed).
TEST(LangFeatures, MatchAutoConstexpr) {
    const std::string gen = e2e::expect_e2e_source("lang_cx_match_auto", R"PURR(import io
constexpr let k = 3
match k {
    case 1 { io.print("a") }
    case 3 { io.print("c") }
    case _ { io.print("z") }
}
)PURR",
                                                   "c\n");
    EXPECT_NE(gen.find("if constexpr ("), std::string::npos);
}

// A plain `match` on a RUNTIME integer lowers to a real C++ `switch` (default = `_`).
TEST(LangFeatures, MatchRuntimeIntSwitch) {
    const std::string gen = e2e::expect_e2e_source("lang_match_switch", R"PURR(import io
fn classify(n: int) {
    match n {
        case 1 { return "a" }
        case 2 { return "b" }
        case _ { return "c" }
    }
}
io.print(classify(1), classify(2), classify(9))
)PURR",
                                                   "a b c\n");
    EXPECT_NE(gen.find("switch ("), std::string::npos) << "integral match must lower to a switch";
    EXPECT_NE(gen.find("default:"), std::string::npos) << "the `_` case must be `default:`";
    EXPECT_EQ(gen.find("if constexpr"), std::string::npos);
}

// GUARD: a `match` on a STRING cannot be a switch (C++ forbids it) — it must fall back to the
// `==` if/else-if chain. The most important correctness guard of the smart lowering.
TEST(LangFeatures, MatchStringFallsBackToChain) {
    const std::string gen = e2e::expect_e2e_source("lang_match_string", R"PURR(import io
fn code(s) {
    match s {
        case "red" { return 1 }
        case "green" { return 2 }
        case _ { return 0 }
    }
}
io.print(code("red"), code("green"), code("blue"))
)PURR",
                                                   "1 2 0\n");
    EXPECT_EQ(gen.find("switch ("), std::string::npos)
        << "a string match must NOT lower to a switch (won't compile)";
}

// GUARD: bool case labels are not switch labels here either -> `==` chain.
TEST(LangFeatures, MatchBoolFallsBackToChain) {
    const std::string gen = e2e::expect_e2e_source("lang_match_bool", R"PURR(import io
fn name(b) {
    match b {
        case true { return "yes" }
        case false { return "no" }
    }
}
io.print(name(true), name(false))
)PURR",
                                                   "yes no\n");
    EXPECT_EQ(gen.find("switch ("), std::string::npos);
}

// A switch case whose body RETURNS must not emit an unreachable trailing `break` (else
// -Wunreachable-code/-Werror could reject it). Verifies it compiles and runs.
TEST(LangFeatures, MatchSwitchCaseReturnsNoUnreachableBreak) {
    e2e::expect_e2e("lang_match_return", R"PURR(import io
fn pick(n: int) {
    match n {
        case 1 { return 10 }
        case 2 { return 20 }
        case _ { return 0 }
    }
}
io.print(pick(1), pick(2), pick(5))
)PURR",
                    "10 20 0\n");
}

// Negative integer case labels are valid switch labels (`case (-1):`).
TEST(LangFeatures, MatchNegativeIntSwitch) {
    const std::string gen = e2e::expect_e2e_source("lang_match_negative", R"PURR(import io
fn sign(n: int) {
    match n {
        case -1 { return "neg" }
        case 0 { return "zero" }
        case _ { return "pos" }
    }
}
io.print(sign(-1), sign(0), sign(7))
)PURR",
                                                   "neg zero pos\n");
    EXPECT_NE(gen.find("switch ("), std::string::npos);
}

// Identifiers that are legal cheatah names but C++ keywords (`delete`, `new`, `default`,
// `switch`, `template`, `typename`, …) are emitted with a trailing `_` so the generated C++
// compiles — applied symmetrically at declaration and every use site (fn/method/field/param/
// var/loop/catch). cheatah's own keyword set is smaller (is_keyword, lexer.cpp), so these
// stay valid source. String-literal contexts (a struct's printed field label) keep the
// ORIGINAL spelling, not the escaped one.
TEST(LangFeatures, CppKeywordIdentifiersAreEscaped) {
    const std::string gen = e2e::expect_e2e_source("lang_cpp_keyword_escape", R"PURR(import io
struct Box {
    new: int
    default: str
    fn delete(self) { return self.new }
}
fn make(new) {
    return Box({.new = new, .default = "d"})
}
let b = make(5)
io.print(b.delete())
io.print(b.default)
let switch = 3
for template in range(0, switch) { io.print(template) }
try { raise "boom" } except typename { io.print(typename) }
)PURR",
                                                   "5\nd\n0\n1\n2\nboom\n");
    // Declarations + use sites escaped: the keyword-named function, field, param, var, and
    // loop/catch variables all carry the trailing underscore in the emitted C++.
    EXPECT_NE(gen.find("delete_("), std::string::npos) << "method name not escaped:\n" << gen;
    EXPECT_NE(gen.find("new_"), std::string::npos) << "field/param name not escaped:\n" << gen;
    EXPECT_NE(gen.find("switch_"), std::string::npos) << "variable name not escaped:\n" << gen;
    EXPECT_NE(gen.find("template_"), std::string::npos) << "loop variable not escaped:\n" << gen;
    // The RAW keyword never appears as a bare C++ identifier (would not compile).
    EXPECT_EQ(gen.find(" delete("), std::string::npos) << "unescaped `delete(` leaked:\n" << gen;
    // A field's printed label keeps the ORIGINAL spelling (string literal, not an identifier).
    EXPECT_NE(gen.find("\"new"), std::string::npos) << "field label should keep raw name:\n" << gen;
}
