#pragma once

/**
 * @file parsers.hpp
 * @brief cheatah `parsers` — fast, safe, from-scratch input parsers. `import parsers` to use it.
 *
 * A C++-authored stdlib module (like socket/hashlib — the parsers use templates, concepts, and
 * std::variant beyond the current .purr subset; `requests` is the first pure-cheatah module).
 * Submodules:
 *
 *   parsers::json — a fast, SIMD-accelerated JSON parser:
 *     * `import parsers.json.Parser as Parser` — the reusable DOM parser (pooled views or a
 *       self-contained owning Document), iterative grammar (no stack overflow at any depth),
 *       compile-time Validate switch, SIMD scanning.
 *     * the typed reader `read<T>()` parses straight into schema'd structs (purrc synthesizes
 *       the schema for .purr structs).
 *   parsers::url — `import parsers.url.Parser as Parser` — the http(s) URL parser.
 *
 * The whole module is fuzz-hardened (prefix/corruption corpora) and runs clean under
 * ASan + UBSan and Valgrind.
 */

#include "json/json.hpp"
#include "json/read.hpp"
#include "url/url.hpp"
