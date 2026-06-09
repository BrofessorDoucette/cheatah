#pragma once

/**
 * @file example.hpp
 * @brief cheatah `example` — a TEMPLATE standard-library extension module.
 *        `import example` (after `biome add cheatah-example`) to use it.
 *
 * Replace this with a real extension (cheatah-gpu / cheatah-plot / cheatah-space /
 * cheatah-learn …). It demonstrates the contract: a `cheatah::<name>` namespace,
 * flat free functions over cheatah's value types, and the house Doxygen
 * convention (`@complexity`/`@alloc`/`@test`).
 */
#include <string>

namespace cheatah::example {

/**
 * A friendly greeting — the smallest possible extension surface.
 * @param who the name to greet.
 * @return the greeting string.
 * @complexity O(n) in the name length.
 * @alloc allocates the returned string.
 * @test CheatahExample.Greet
 */
std::string greet(const std::string& who);

} // namespace cheatah::example
