#pragma once

/**
 * @file xml.hpp
 * @brief `parsers.xml` — a small, tolerant, from-scratch XML reader. `import parsers.xml`.
 *
 * A submodule of `parsers` (alongside `parsers.json`, `parsers.url`, `parsers.html`). It
 * parses an XML document into a **slab DOM**: every element and text run is a `Node` stored
 * in one flat vector inside the `Document`, and the tree is navigated by **integer node id**
 * — never by pointer. That keeps the whole surface value-semantic and safe to drive from
 * cheatah (ids + lists, no owning handles to leak): a `.purr` program parses once and walks
 * the tree with `find` / `findall` / `iter` and `attr` / `text`.
 *
 * It exists to feed cheatah's own tooling (e.g. reading Doxygen XML), so it targets the
 * pragmatic subset real tools emit: elements, attributes, text, character references
 * (`&lt; &gt; &amp; &quot; &apos; &#NN; &#xNN;`), `<!-- comments -->`, `<![CDATA[…]]>`, the
 * `<?xml …?>` prolog and processing instructions, and `<!DOCTYPE …>`. Parsing is **iterative**
 * (an explicit open-element stack — no recursion, so no stack overflow at any nesting depth)
 * and **lenient**: malformed markup never throws; unterminated constructs consume to
 * end-of-input and a mismatched close tag is tolerated.
 */
#include <string>
#include <string_view>
#include <vector>

namespace cheatah::parsers::xml {

/// One attribute of an element: `name="value"` (value already reference-decoded).
struct Attr {
    std::string name;   ///< the attribute name.
    std::string value;  ///< the decoded attribute value.
};

/// One DOM node: an **element** (`is_element` true — has @ref tag, @ref attrs, @ref children)
/// or a **text** run (`is_element` false — @ref text holds the decoded character data). Child
/// order (elements and text interleaved) is preserved via @ref children, which lists child
/// node **ids** into the owning @ref Document.
struct Node {
    bool is_element = false;        ///< true: an element; false: a text run.
    std::string tag;                ///< element tag name (empty for text).
    std::string text;               ///< decoded text (for a text node; empty for elements).
    std::vector<Attr> attrs;        ///< element attributes (empty for text).
    std::vector<int> children;      ///< ids of child nodes, in document order.
};

/// A parsed document: the node slab plus the id of the synthetic **root** (the root's
/// @ref Node::children are the document's top-level nodes, so a prolog/comment before the
/// root element is kept as siblings). Navigate everything by id through the functions below.
struct Document {
    std::vector<Node> nodes;   ///< every node; index == its id.
    int root = 0;              ///< id of the root container node.
};

/**
 * Parse @p xml into a @ref Document (slab DOM). Never throws.
 *
 * The returned document always has at least the root node (`root`). Malformed input is
 * tolerated: unterminated tags/comments/CDATA consume to end-of-input, and a `</x>` with no
 * matching open element is ignored rather than aborting the parse.
 * @param xml the document text.
 * @return the parsed document (root-only for empty/blank input).
 * @complexity O(n) time in the length of @p xml.
 * @alloc the node slab and its strings on the heap (owned by the returned Document).
 */
Document parse(std::string_view xml);

/**
 * The document root's node id (its children are the top-level nodes).
 * @complexity O(1).
 * @alloc none.
 */
int root(const Document& doc);

/**
 * Whether @p id is a valid element node in @p doc.
 * @complexity O(1).
 * @alloc none.
 */
bool is_element(const Document& doc, int id);

/**
 * The tag name of element @p id, or "" if @p id is not an element.
 * @complexity O(1).
 * @alloc one result string.
 */
std::string tag(const Document& doc, int id);

/**
 * Value of attribute @p name on element @p id, or "" if absent (or @p id is not an element).
 * @complexity O(k) in the attribute count of @p id.
 * @alloc one result string.
 */
std::string attr(const Document& doc, int id, std::string_view name);

/**
 * Whether element @p id carries an attribute named @p name.
 * @complexity O(k) in the attribute count of @p id.
 * @alloc none.
 */
bool has_attr(const Document& doc, int id, std::string_view name);

/**
 * The concatenated text of node @p id: for a text node, its own text; for an element, all
 * descendant text in document order (like an XML `.textContent`).
 * @complexity O(m) in the number of descendants of @p id.
 * @alloc one result string.
 */
std::string text(const Document& doc, int id);

/**
 * The child node ids of @p id, in document order (elements and text).
 * @complexity O(1) (returns a copy of the id list).
 * @alloc the id list.
 */
std::vector<int> children(const Document& doc, int id);

/**
 * The id of the **first child element** of @p id whose tag equals @p tag, or -1 if none.
 * (Direct children only — not descendants; use @ref iter for the whole subtree.)
 * @complexity O(c) in the direct-child count of @p id.
 * @alloc none.
 */
int find(const Document& doc, int id, std::string_view tag);

/**
 * The ids of **all direct child elements** of @p id whose tag equals @p tag, in order.
 * @complexity O(c) in the direct-child count of @p id.
 * @alloc the result id list.
 */
std::vector<int> findall(const Document& doc, int id, std::string_view tag);

/**
 * The ids of **every element in the subtree** rooted at @p id (including @p id itself) whose
 * tag equals @p tag, in document order — the analogue of an XML tree `.iter(tag)`.
 * @complexity O(m) in the subtree size of @p id.
 * @alloc the result id list (and a transient walk stack).
 */
std::vector<int> iter(const Document& doc, int id, std::string_view tag);

}  // namespace cheatah::parsers::xml
