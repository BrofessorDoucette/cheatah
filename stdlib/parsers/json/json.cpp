// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// json.cpp — implementation of the cheatah::parsers::json parser/serializer. All multi-line
// logic lives here (declared in json.hpp); only trivial getters live in the token headers.
//
// Node is a class wrapping a std::variant, so its alternatives are reached via node.variant().
// Dispatch is std::visit over that variant — compile-time, no runtime polymorphism.

#include "json.hpp"

#include "cursor.hpp"
#include "owning_builder.hpp"  // OwningBuilder (the owning construction policy)
#include "scan.hpp"            // detail:: low-level scanners (shared with the struct reader)
#include "simd.hpp"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace cheatah::parsers::json {

namespace {

// The low-level scanners live in scan.hpp (detail::), shared verbatim with the struct reader; pull
// the ones the DOM grammar uses into this scope.
using detail::decode_escapes;
using detail::match;
using detail::scan_string;
using detail::skip_ws;

// Parse a JSON string straight into `out`. OwnsStrings is the builder's string policy: when false
// (pooled/view path) an unescaped string is a zero-copy String<std::string_view> into the SOURCE
// text; when true (owning path) it is COPIED into an owned String<std::string> so the Document is
// self-contained and may outlive the source (e.g. the cache parses a temporary buffer). An ESCAPED
// string is always decoded into owned storage regardless, since the decoded bytes have no source.
// @complexity O(|string|)  @alloc only for owned/escaped strings  @test Json.Strings
template <bool OwnsStrings>
bool parse_string(Cursor& c, Node& out) {
    std::string_view raw;
    bool esc = false;
    if (!scan_string(c, raw, esc)) {
        return false;
    }
    if (!esc) {
        if constexpr (OwnsStrings) {
            out.variant().emplace<String<std::string>>(std::string(raw));  // owned copy
        } else {
            out.variant().emplace<String<std::string_view>>(raw);  // zero-copy view into source
        }
        return true;
    }
    std::string decoded;
    if (!decode_escapes(raw, decoded)) {
        return false;
    }
    out.variant().emplace<String<std::string>>(std::move(decoded));  // owned
    return true;
}

// Parse a JSON number into `out` (emplaced in place).
// @complexity O(digits)  @alloc none  @test Json.Numbers
bool parse_number(Cursor& c, Node& out) {
    double d = 0.0;
    if (!detail::parse_arithmetic(c, d)) {
        return false;
    }
    out.variant().emplace<Number>(d);
    return true;
}

// ---- serialization (re-escapes string contents) -----------------------------

// Append `s` to `out` as a quoted JSON string, re-escaping specials and control bytes (\u00XX).
// @complexity O(|s|)  @alloc amortized `out` growth  @test Json.DumpRoundTrip
void dump_string(std::string_view s, std::string& out) {
    static constexpr char kHex[] = "0123456789abcdef";
    out.push_back('"');
    for (const char ch : s) {
        switch (ch) {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b");  break;
            case '\f': out.append("\\f");  break;
            case '\n': out.append("\\n");  break;
            case '\r': out.append("\\r");  break;
            case '\t': out.append("\\t");  break;
            default:
                if (static_cast<unsigned char>(ch) < 0x20) {
                    out.append("\\u00");
                    out.push_back(kHex[(static_cast<unsigned char>(ch) >> 4) & 0xF]);
                    out.push_back(kHex[static_cast<unsigned char>(ch) & 0xF]);
                } else {
                    out.push_back(ch);
                }
        }
    }
    out.push_back('"');
}

// Serialize ITERATIVELY with an explicit stack of open containers — symmetric with the iterative
// parser, so a document of any nesting depth dumps without exhausting the C++ call stack (the
// parser accepts adversarially deep input; the serializer must survive it too).
// @complexity O(nodes)  @alloc the frame stack, O(depth)  @test Json.DeepDumpRoundTrip
void dump_to(const Node& root, std::string& out) {
    // One open container being emitted: which kind, how many children are already written, and a
    // span of its remaining children (exactly one of the spans is used, chosen by is_object).
    struct Frame {
        bool is_object;
        std::size_t next;
        std::span<const Node> elements;
        std::span<const Member> members;
    };
    std::vector<Frame> stack;
    const Node* value = &root;

    for (;;) {
        // (A) Emit one VALUE. Scalars are appended whole; a container appends its opener and pushes
        //     a frame, so its children are emitted by the loop below.
        std::visit(
            [&](const auto& tok) {
                using T = std::decay_t<decltype(tok)>;
                if constexpr (std::is_same_v<T, Null>) {
                    out.append("null");
                } else if constexpr (std::is_same_v<T, Boolean>) {
                    out.append(tok.value() ? "true" : "false");
                } else if constexpr (std::is_same_v<T, Number>) {
                    char buf[32];
                    const std::to_chars_result r = std::to_chars(buf, buf + sizeof buf, tok.value());
                    out.append(buf, r.ptr);
                } else if constexpr (std::is_same_v<T, String<std::string_view>> ||
                                     std::is_same_v<T, String<std::string>>) {
                    dump_string(tok.value(), out);
                } else if constexpr (std::is_same_v<T, OwnedArray> || std::is_same_v<T, ArrayView>) {
                    out.push_back('[');
                    stack.push_back(Frame{.is_object = false,
                                          .next = 0,
                                          .elements = tok.value(),
                                          .members = {}});
                } else {  // Object
                    out.push_back('{');
                    stack.push_back(Frame{.is_object = true,
                                          .next = 0,
                                          .elements = {},
                                          .members = tok.value()});
                }
            },
            value->variant());

        // (B) Find the next value to emit: write separators/keys for the innermost open container,
        //     closing every container that has run out of children along the way.
        value = nullptr;
        while (!stack.empty() && value == nullptr) {
            Frame& top = stack.back();
            if (top.is_object) {
                if (top.next == top.members.size()) {
                    out.push_back('}');
                    stack.pop_back();
                    continue;  // this object is finished — keep ascending
                }
                if (top.next > 0) {
                    out.push_back(',');
                }
                const Member& m = top.members[top.next++];
                dump_string(to_view(m.first), out);  // the key (a string Node)
                out.push_back(':');
                value = &m.second;
            } else {
                if (top.next == top.elements.size()) {
                    out.push_back(']');
                    stack.pop_back();
                    continue;  // this array is finished — keep ascending
                }
                if (top.next > 0) {
                    out.push_back(',');
                }
                value = &top.elements[top.next++];
            }
        }
        if (value == nullptr) {
            return;  // stack empty: the whole document has been emitted
        }
    }
}

}  // namespace

std::string_view to_view(const Node& value) noexcept {
    const Node::variant_type& v = value.variant();
    if (const String<std::string_view>* view = std::get_if<String<std::string_view>>(&v)) {
        return view->value();
    }
    if (const String<std::string>* owned = std::get_if<String<std::string>>(&v)) {
        return owned->value();
    }
    return {};
}

template <bool Validate>
Document parse(std::string_view text, bool* ok) {
    // A fresh Parser yields a self-contained OWNING Document (no pool references), so it is safe to
    // return even though the Parser is destroyed here. Reuse a Parser (Parser::parse for the pooled
    // view form, or Parser::parse_owning) to amortize allocation across many parses. Validate
    // forwards straight through to the grammar (default true; <false> compiles the checks out).
    Parser p;
    return p.parse_owning<Validate>(text, ok);
}
template Document parse<true>(std::string_view, bool*);
template Document parse<false>(std::string_view, bool*);

void dump(const Document& value, std::string& out) {
    dump_to(value, out);  // appends into the caller's (preallocated/reused) buffer
}

std::string dump(const Document& value) {
    std::string out;
    dump_to(value, out);
    return out;
}

// ---- reusable Parser: ONE iterative grammar, two Builders -------------------
//
// The recursion is unrolled into a loop over an explicit frame_stack_: each open container is a
// Frame, the Builder (driven as a stack machine: begin/add/finish) holds the partial container, and
// completed values "bubble up" to their parent. Nesting depth therefore costs heap, not C++ call
// frames — no stack overflow on adversarially deep input. The grammar is parameterized on (1) a
// compile-time `bool Validate` and (2) a Builder policy (PoolBuilder -> span VIEWS into a reused
// pool, or OwningBuilder -> self-contained owned vectors). Every bounds/structure check sits inside
// `if constexpr (Validate)`, so with Validate=false the standard DISCARDS those statements at
// compile time (validation gone from the binary, not merely optimized away); trusted input is
// assumed well-formed, so the elided checks never fire and the result is identical (malformed input
// under Validate=false is UB). Instantiated for both modes and both builders below.

// Maximum container-nesting depth accepted under validation. The PARSE is iterative (frame_stack_),
// but the OWNING result is a recursively-nested vector<Node> tree whose compiler-generated destructor
// recurses one C++ frame per level — so genuinely-deep (but syntactically valid) input like
// `[[[…]]]` would overflow the stack on scope-exit. Capping nesting during the parse transitively
// bounds the tree depth, and thus that destructor (and any recursive accessor). 1000 is far deeper
// than any real document and far below where the destructor overflows. Trusted-input (Validate=false)
// callers skip the check, matching the module's "no validation on the trusted fast path" contract.
inline constexpr std::size_t kMaxParseDepth = 1000;

template <bool Validate, class Builder>
bool Parser::parse_value(Cursor& c, Node& out, Builder& b) {
    frame_stack_.clear();
    Node value;  // the most-recently-completed value, bubbling up toward its parent / the root

    for (;;) {
        // (A) Inside an object, the next child is a member: read its "key" and the ':' first.
        if (!frame_stack_.empty() && frame_stack_.back().is_object) {
            skip_ws(c);
            if constexpr (Validate) {
                if (c.it == c.end || *c.it != '"') {
                    return false;  // key must be a string
                }
            }
            if (!parse_string<Builder::owns_strings>(c, frame_stack_.back().key)) {
                return false;
            }
            skip_ws(c);
            if constexpr (Validate) {
                if (c.it == c.end || *c.it != ':') {
                    return false;
                }
            }
            ++c.it;  // skip ':'
        }

        // (B) Read one value. A scalar fills `value`; an opening '['/'{' begins a container and (if
        //     non-empty) pushes a frame and loops back to read its first child. An empty container
        //     is finished immediately and falls through to (C).
        skip_ws(c);
        if constexpr (Validate) {
            if (c.it == c.end) {
                return false;
            }
        }
        bool opened = false;
        switch (*c.it) {
            case '[':
                ++c.it;
                b.begin_array();
                skip_ws(c);
                if constexpr (Validate) {
                    if (c.it == c.end) {
                        return false;  // unterminated right after '['
                    }
                }
                if (*c.it == ']') {
                    ++c.it;
                    value = b.finish_array();  // empty array
                } else {
                    if constexpr (Validate) {
                        if (frame_stack_.size() >= kMaxParseDepth) return false;  // nesting too deep
                    }
                    frame_stack_.push_back(Frame{false, Node{}});
                    opened = true;
                }
                break;
            case '{':
                ++c.it;
                b.begin_object();
                skip_ws(c);
                if constexpr (Validate) {
                    if (c.it == c.end) {
                        return false;  // unterminated right after '{'
                    }
                }
                if (*c.it == '}') {
                    ++c.it;
                    value = b.finish_object();  // empty object
                } else {
                    if constexpr (Validate) {
                        if (frame_stack_.size() >= kMaxParseDepth) return false;  // nesting too deep
                    }
                    frame_stack_.push_back(Frame{true, Node{}});
                    opened = true;
                }
                break;
            case '"':
                if (!parse_string<Builder::owns_strings>(c, value)) {
                    return false;
                }
                break;
            case 't':
                if (!match(c, "true")) {
                    return false;
                }
                value.variant().emplace<Boolean>(true);
                break;
            case 'f':
                if (!match(c, "false")) {
                    return false;
                }
                value.variant().emplace<Boolean>(false);
                break;
            case 'n':
                if (!match(c, "null")) {
                    return false;
                }
                value.variant().emplace<Null>();
                break;
            default:
                if (!parse_number(c, value)) {  // digit or '-'
                    return false;
                }
                break;
        }
        if (opened) {
            continue;  // a non-empty container was opened: loop back to read its first child/key
        }

        // (C) `value` is complete. Attach it to the enclosing container, then ascend through every
        //     container that closes here (each closed container becomes the next `value`).
        for (;;) {
            if (frame_stack_.empty()) {
                out = std::move(value);  // `value` was the whole document
                return true;
            }
            Frame& top = frame_stack_.back();
            if (top.is_object) {
                b.add_member(Member{std::move(top.key), std::move(value)});
            } else {
                b.add_element(std::move(value));
            }
            skip_ws(c);
            if constexpr (Validate) {
                if (c.it == c.end) {
                    return false;
                }
            }
            const char sep = *c.it++;
            const char closer = top.is_object ? '}' : ']';
            if (sep == ',') {
                break;  // more children: loop back to (A)/(B)
            }
            if (sep == closer) {
                value = top.is_object ? b.finish_object() : b.finish_array();
                frame_stack_.pop_back();
                continue;  // ascend: the closed container is now `value` for ITS parent
            }
            return false;  // expected ',' or the closing bracket/brace
        }
    }
}

// In the Validate=false instantiations `ok` is never referenced (the whole else branch below omits
// it), so it is [[maybe_unused]]; the unchecked binary contains no *ok store at all.
template <bool Validate>
Document Parser::parse(std::string_view text, [[maybe_unused]] bool* ok) {
    pool_.reset(text.size());  // reuse capacity; reserve a safe upper bound so spans never dangle

    Cursor c{text.data(), text.data() + text.size()};
    Node root;  // default-constructed to null
    if constexpr (Validate) {
        bool good = parse_value<true>(c, root, pool_);  // ArrayView/ObjectView into the reused pool
        if (good) {
            skip_ws(c);
            good = (c.it == c.end);  // reject trailing junk
        }
        if (!good) {
            root.variant().emplace<Null>();
        }
        if (ok != nullptr) {
            *ok = good;
        }
    } else {
        // Trusted input: parse with NO validation — no bounds/structure checks, no trailing-junk
        // check, and `ok` is not touched (an unchecked parse has no validity to report; malformed
        // input is undefined behavior, not a reported error).
        parse_value<false>(c, root, pool_);
    }
    return root;
}

template <bool Validate>
Document Parser::parse_owning(std::string_view text, [[maybe_unused]] bool* ok) {
    Cursor c{text.data(), text.data() + text.size()};
    Node root;  // default-constructed to null
    OwningBuilder owning;  // local vectors -> OwnedArray/OwnedObject (no references into this Parser)
    if constexpr (Validate) {
        bool good = parse_value<true>(c, root, owning);
        if (good) {
            skip_ws(c);
            good = (c.it == c.end);  // reject trailing junk
        }
        if (!good) {
            root.variant().emplace<Null>();
        }
        if (ok != nullptr) {
            *ok = good;
        }
    } else {
        parse_value<false>(c, root, owning);  // trusted input: no validation, `ok` left untouched
    }
    return root;
}

// Instantiate both validation modes of each public entry point (the private grammar they call is
// implicitly instantiated for both Builders along with them). parse<true>/parse_owning<true> are
// the default; the <false> forms compile with all validation `if constexpr`-stripped.
template Document Parser::parse<true>(std::string_view, bool*);
template Document Parser::parse<false>(std::string_view, bool*);
template Document Parser::parse_owning<true>(std::string_view, bool*);
template Document Parser::parse_owning<false>(std::string_view, bool*);

std::string_view Parser::dump(const Document& value) {
    dump_buf_.clear();           // reuse the buffer's capacity across calls (no realloc once warm)
    dump_to(value, dump_buf_);
    return dump_buf_;            // valid until the next dump() on this Parser
}

}  // namespace cheatah::parsers::json
