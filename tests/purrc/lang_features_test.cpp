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

TEST(LangFeatures, AppendAndDictMutation) {
    e2e::expect_e2e("lang_append", R"PURR(import io
let xs: list[int] = []
append(xs, 1)
xs.append(2)
xs.append(3)
io.print(len(xs), xs[2])
let counts: dict[str, int] = {}
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
