#pragma once

/**
 * @file html.hpp
 * @brief `parsers.html` — HTML escaping plus a tolerant tokenizing parser, the
 *        rough equivalent of Python's `html` module + `html.parser`.
 *
 * A submodule of `parsers` (alongside `parsers.json` and `parsers.url`): `import
 * parsers.html`, then call `parsers.html.escape(...)`, `parsers.html.unescape(...)`,
 * and `parsers.html.parse(...)`.
 *
 * **Why this shape.** Python's `html.parser.HTMLParser` is used by *subclassing* it
 * and overriding callbacks (`handle_starttag`, `handle_data`, …). cheatah has no
 * classes-with-methods, inheritance, or callbacks, so the events become **data**:
 * `parsers.html.parse(src)` returns the list of tokens a callback-based parser would
 * have dispatched, in document order. A `.purr` program walks the list with a `for`
 * loop and a `match` on `tok.kind` instead of overriding methods.
 * `parsers.html.escape` / `parsers.html.unescape` mirror the top-level `html` module.
 *
 * The parser is lenient (it never throws on malformed markup): unterminated
 * comments/tags consume to end-of-input, stray `<` is treated as text, and the
 * raw-text elements `<script>`/`<style>` are read verbatim to their close tag.
 */
#include <string>
#include <string_view>
#include <vector>

namespace cheatah::parsers::html {

/**
 * Escape the markup-significant characters in @p s (Python `html.escape`).
 *
 * Replaces `&`→`&amp;`, `<`→`&lt;`, `>`→`&gt;` always, and when @p quote is
 * true (the default) also `"`→`&quot;` and `'`→`&#x27;`, so the result is safe
 * to drop into element text *and* into a quoted attribute value.
 * @param s the text to escape.
 * @param quote also escape the quote characters (default true).
 * @return a newly allocated escaped copy.
 * @complexity O(n) time in the length of @p s.
 * @alloc one result string on the heap.
 */
std::string escape(std::string_view s, bool quote = true);

/**
 * Resolve character references back to text (Python `html.unescape`).
 *
 * Decodes decimal (`&#169;`) and hexadecimal (`&#xA9;`) numeric references and a
 * table of the common named entities (`&amp;`, `&lt;`, `&gt;`, `&quot;`,
 * `&apos;`, `&nbsp;`, `&copy;`, …) to UTF-8. Unknown or malformed references are
 * left verbatim. Inverse of @ref escape for the characters it covers.
 * @param s text that may contain character references.
 * @return a newly allocated decoded copy.
 * @complexity O(n) time in the length of @p s.
 * @alloc one result string on the heap.
 */
std::string unescape(std::string_view s);

/// One attribute of a start tag: `name="value"`. Valueless attributes (`<input
/// disabled>`) carry an empty @ref value. Names are lowercased.
struct Attr {
    std::string name;   ///< the attribute name, lowercased.
    std::string value;  ///< the (unescaped) attribute value, or "" if valueless.
};

/// One parse event, the data form of an `HTMLParser` callback. Which fields are
/// populated depends on @ref kind (see @ref parse).
struct Token {
    std::string kind;          ///< event kind, see @ref parse.
    std::string tag;           ///< tag name (lowercased) for start/startend/end; else "".
    std::string data;          ///< text/comment/decl/PI payload; else "".
    std::vector<Attr> attrs;   ///< attributes for start/startend tags; else empty.
};

/**
 * Tokenize an HTML document into the events a callback parser would dispatch.
 *
 * Returns one @ref Token per event, in document order. The @ref Token::kind is
 * one of:
 * - `"starttag"`    — `<div class="x">`; @ref Token::tag + @ref Token::attrs set.
 * - `"startendtag"` — self-closing `<br/>`; tag + attrs set.
 * - `"endtag"`      — `</div>`; @ref Token::tag set.
 * - `"data"`        — text between tags; @ref Token::data set (character
 *                     references decoded, like Python's `convert_charrefs=True`).
 * - `"comment"`     — `<!-- … -->`; @ref Token::data is the comment body.
 * - `"decl"`        — `<!DOCTYPE html>`; @ref Token::data is the text after `<!`.
 * - `"pi"`          — processing instruction `<? … >`; @ref Token::data the body.
 *
 * Tag and attribute names are lowercased. `<script>` / `<style>` bodies are
 * emitted as a single raw `"data"` token (references **not** decoded). Malformed
 * input never throws: unterminated constructs consume to end-of-input.
 * @param html the document text.
 * @return the token list (empty for empty input).
 * @complexity O(n) time in the length of @p html.
 * @alloc the token vector and its strings on the heap.
 */
std::vector<Token> parse(std::string_view html);

/**
 * Value of attribute @p name on @p t, or "" if absent (or valueless).
 * @param t a start/startend token.
 * @param name attribute name (matched case-insensitively).
 * @return the attribute value, or "".
 * @complexity O(k) in the attribute count of @p t.
 * @alloc one result string on the heap.
 */
std::string get_attr(const Token& t, std::string_view name);

/**
 * Whether @p t carries an attribute named @p name (case-insensitive).
 * @param t a start/startend token.
 * @param name attribute name.
 * @return true if present (even if valueless).
 * @complexity O(k) in the attribute count of @p t.
 * @alloc none.
 */
bool has_attr(const Token& t, std::string_view name);

}  // namespace cheatah::parsers::html
