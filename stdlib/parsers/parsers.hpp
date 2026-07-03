// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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
 *   parsers::html — `import parsers.html` — HTML escaping (`parsers.html.escape` /
 *     `unescape`) plus a tolerant tokenizing parser (`parsers.html.parse`), the rough
 *     equivalent of Python's `html` module + `html.parser`.
 *
 *   parsers::xml — `import parsers.xml` — a tolerant XML reader that parses into a slab DOM
 *     navigated by integer node id (`parsers.xml.parse`, then `find`/`findall`/`iter` +
 *     `attr`/`text`); value-semantic, no owning handles.
 *
 * The whole module is fuzz-hardened (prefix/corruption corpora) and runs clean under
 * ASan + UBSan and Valgrind.
 */

#include "html/html.hpp"
#include "json/json.hpp"
#include "json/read.hpp"
#include "url/url.hpp"
#include "xml/xml.hpp"
