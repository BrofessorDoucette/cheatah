// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run tests for the `fixarray` module CALLED FROM CHEATAH: `import fixarray`, construct a
// fixed-extent vector/matrix, and use its operations. Proves the whole surface a .purr program
// touches — construction (alias, explicit `Fixed<T, Dims>`, and narrow-width elements), typed
// `let`/param/return annotations for module-qualified types, and the free-function ops — lowers and
// runs. A `Fixed` prints only through a scalar reduction (dot/norm), so results are asserted that
// way.
#include "e2e_harness.hpp"

TEST(FixarrayCompileRun, ConstructAndDot) {
    // The zero-change path: import, construct with the alias, operate. `dot` of (1,2,3)·(4,5,6)=32.
    e2e::expect_e2e("fixarray_dot", R"PURR(import io
import fixarray
fn main() {
    let a = fixarray.vec3f(1.0, 2.0, 3.0)
    let b = fixarray.vec3f(4.0, 5.0, 6.0)
    io.print(fixarray.dot(a, b))
}
main()
)PURR", "32\n");
}

TEST(FixarrayCompileRun, CrossNormalizeAndAdd) {
    // cross(x̂, ŷ) = ẑ (unit); normalize(0,3,4) has length 1; (1,2,3)+(4,5,6)=(5,7,9), |·|²=155.
    e2e::expect_e2e("fixarray_ops", R"PURR(import io
import fixarray
fn main() {
    let z = fixarray.cross(fixarray.vec3f(1.0, 0.0, 0.0), fixarray.vec3f(0.0, 1.0, 0.0))
    io.print(fixarray.dot(z, z))
    let n = fixarray.normalize(fixarray.vec3f(0.0, 3.0, 4.0))
    io.print(fixarray.dot(n, n))
    let a = fixarray.vec3f(1.0, 2.0, 3.0)
    let s = a + fixarray.vec3f(4.0, 5.0, 6.0)
    io.print(fixarray.dot(s, s))
}
main()
)PURR", "1\n1\n155\n");
}

TEST(FixarrayCompileRun, TypedLetAndExplicitFixed) {
    // Module-qualified type annotations: a plain alias, an explicit `Fixed<f32, 3>`, and a struct
    // field of a fixarray type. All lower to `::cheatah::fixarray::…` and interoperate.
    std::string src = e2e::expect_e2e_source("fixarray_typed", R"PURR(import io
import fixarray
struct Body {
    pos: fixarray.vec3f
    vel: fixarray.vec3f
}
fn main() {
    let v: fixarray.vec3f = fixarray.vec3f(1.0, 2.0, 3.0)
    let m: fixarray.Fixed<f32, 3> = fixarray.vec3f(2.0, 0.0, 0.0)
    io.print(fixarray.dot(v, m))
    let b: Body = Body(fixarray.vec3f(0.0, 0.0, 0.0), fixarray.vec3f(1.0, 1.0, 1.0))
    io.print(fixarray.dot(b.vel, b.vel))
}
main()
)PURR", "2\n3\n");
    EXPECT_NE(src.find("fixarray::Fixed<float, 3> m"), std::string::npos) << src;
    EXPECT_NE(src.find("fixarray::vec3f pos;"), std::string::npos) << src;
}

TEST(FixarrayCompileRun, TypedParamAndReturn) {
    // A fixarray-typed parameter and return — `Fixed<f32, 3>` with a numeric extent — parse and
    // lower; passing a named vector (an lvalue) binds the concrete reference the signature uses.
    std::string src = e2e::expect_e2e_source("fixarray_param", R"PURR(import io
import fixarray
fn length2(v: fixarray.Fixed<f32, 3>) -> f32 {
    return fixarray.dot(v, v)
}
fn main() {
    let a = fixarray.vec3f(1.0, 2.0, 3.0)
    io.print(length2(a))
}
main()
)PURR", "14\n");
    EXPECT_NE(src.find("float length2(fixarray::Fixed<float, 3>"), std::string::npos) << src;
}

TEST(FixarrayCompileRun, NarrowWidthElements) {
    // A narrow-element vector: `Fixed<u8, N>` / `Vec<i16, N>` lower the width into the element type
    // (a smaller footprint), constructible and usable from cheatah. Construction converts each value
    // to the element type; the vector is then a normal fixarray value.
    std::string src = e2e::expect_e2e_source("fixarray_narrow", R"PURR(import io
import fixarray
fn main() {
    let v: fixarray.Vec<u8, 3> = fixarray.Vec<u8, 3>(1, 2, 3)
    io.print(fixarray.dot(v, v))
    let w: fixarray.Vec<i16, 4> = fixarray.Vec<i16, 4>(1, 2, 3, 4)
    io.print(fixarray.dot(w, w))
}
main()
)PURR", "14\n30\n");
    EXPECT_NE(src.find("fixarray::Vec<std::uint8_t, 3>"), std::string::npos) << src;
    EXPECT_NE(src.find("fixarray::Vec<std::int16_t, 4>"), std::string::npos) << src;
}
