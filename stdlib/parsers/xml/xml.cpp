#include "xml.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cheatah::parsers::xml {

namespace {

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

bool is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
}
bool is_name_char(char c) {
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

void append_utf8(std::string& out, std::uint32_t cp) {
    if (cp <= 0x7F) {
        out.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

// Decode a reference body (between '&' and ';'). true+append on success; false leaves it.
bool decode_reference(std::string_view body, std::string& out) {
    if (body.empty()) return false;
    if (body.front() == '#') {  // numeric &#169; / &#xA9;
        std::uint32_t cp = 0;
        const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
        std::size_t i = hex ? 2 : 1;
        if (i >= body.size()) return false;
        for (; i < body.size(); ++i) {
            const char c = body[i];
            std::uint32_t d;
            if (c >= '0' && c <= '9') d = static_cast<std::uint32_t>(c - '0');
            else if (hex && c >= 'a' && c <= 'f') d = static_cast<std::uint32_t>(c - 'a' + 10);
            else if (hex && c >= 'A' && c <= 'F') d = static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
            cp = cp * (hex ? 16 : 10) + d;
            if (cp > 0x10FFFF) return false;
        }
        if (cp == 0) return false;
        append_utf8(out, cp);
        return true;
    }
    // The five XML predefined entities.
    if (body == "amp") { out.push_back('&'); return true; }
    if (body == "lt") { out.push_back('<'); return true; }
    if (body == "gt") { out.push_back('>'); return true; }
    if (body == "quot") { out.push_back('"'); return true; }
    if (body == "apos") { out.push_back('\''); return true; }
    return false;
}

// Resolve character references in `s` (unknown refs are kept verbatim, like a lenient reader).
std::string decode(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '&') { out.push_back(s[i++]); continue; }
        const std::size_t semi = s.find(';', i + 1);
        const std::size_t limit = i + 1 + 32;  // entity names are short
        if (semi != std::string_view::npos && semi <= limit && semi > i + 1 &&
            decode_reference(s.substr(i + 1, semi - i - 1), out)) {
            i = semi + 1;
        } else {
            out.push_back(s[i++]);
        }
    }
    return out;
}

// The parser: builds the slab DOM iteratively with an explicit open-element stack.
struct Parser {
    std::string_view s;
    std::size_t i = 0;
    Document doc;
    std::vector<int> open;  // stack of open element ids (open.back() is the current parent)

    int current() const { return open.back(); }

    int add_child(Node&& n) {
        const int id = static_cast<int>(doc.nodes.size());
        doc.nodes.push_back(std::move(n));
        doc.nodes[static_cast<std::size_t>(current())].children.push_back(id);
        return id;
    }

    void flush_text(std::size_t from, std::size_t to) {
        if (to <= from) return;
        std::string t = decode(s.substr(from, to - from));
        // Keep only text that has non-whitespace, OR any text inside an element (preserves
        // significant whitespace); drop pure-whitespace between top-level nodes.
        bool has_nonspace = false;
        for (const char c : t) if (!is_space(c)) { has_nonspace = true; break; }
        if (!has_nonspace && open.size() == 1) return;  // ignorable whitespace at document top
        Node n;
        n.is_element = false;
        n.text = std::move(t);
        add_child(std::move(n));
    }

    // Parse the attributes of a start tag beginning at s[i] (i points just past the name).
    // Stops at '>' / '/>' / end-of-input. Sets self_close.
    void parse_attrs(Node& el, bool& self_close) {
        const std::size_t n = s.size();
        while (i < n && s[i] != '>') {
            while (i < n && is_space(s[i])) ++i;
            if (i < n && s[i] == '/') { self_close = true; ++i; continue; }
            if (i >= n || s[i] == '>') break;
            const std::size_t an = i;
            while (i < n && !is_space(s[i]) && s[i] != '=' && s[i] != '>' && s[i] != '/') ++i;
            if (i == an) { ++i; continue; }  // stray char
            std::string name(s.substr(an, i - an));
            std::string value;
            while (i < n && is_space(s[i])) ++i;
            if (i < n && s[i] == '=') {
                ++i;
                while (i < n && is_space(s[i])) ++i;
                if (i < n && (s[i] == '"' || s[i] == '\'')) {
                    const char q = s[i++];
                    const std::size_t vs = i;
                    while (i < n && s[i] != q) ++i;
                    value = decode(s.substr(vs, i - vs));
                    if (i < n) ++i;  // closing quote
                } else {
                    const std::size_t vs = i;
                    while (i < n && !is_space(s[i]) && s[i] != '>' && s[i] != '/') ++i;
                    value = decode(s.substr(vs, i - vs));
                }
            }
            el.attrs.push_back({std::move(name), std::move(value)});
        }
    }

    void run() {
        const std::size_t n = s.size();
        // The synthetic root.
        doc.nodes.push_back(Node{});
        doc.root = 0;
        open.push_back(0);

        std::size_t text_from = 0;
        while (i < n) {
            if (s[i] != '<') { ++i; continue; }
            flush_text(text_from, i);

            if (s.compare(i, 4, "<!--") == 0) {  // comment
                const std::size_t end = s.find("-->", i + 4);
                i = (end == std::string_view::npos) ? n : end + 3;
                text_from = i;
                continue;
            }
            if (s.compare(i, 9, "<![CDATA[") == 0) {  // CDATA -> literal text (not decoded)
                const std::size_t start = i + 9;
                const std::size_t end = s.find("]]>", start);
                const std::size_t stop = (end == std::string_view::npos) ? n : end;
                Node t;
                t.is_element = false;
                t.text = std::string(s.substr(start, stop - start));
                add_child(std::move(t));
                i = (end == std::string_view::npos) ? n : end + 3;
                text_from = i;
                continue;
            }
            if (i + 1 < n && (s[i + 1] == '?' || s[i + 1] == '!')) {  // prolog / PI / DOCTYPE
                const std::size_t end = s.find('>', i + 2);
                i = (end == std::string_view::npos) ? n : end + 1;
                text_from = i;
                continue;
            }
            if (i + 1 < n && s[i + 1] == '/') {  // end tag </name>
                const std::size_t ns = i + 2;
                std::size_t j = ns;
                while (j < n && is_name_char(s[j])) ++j;
                const std::string_view name = s.substr(ns, j - ns);
                std::size_t end = s.find('>', j);
                i = (end == std::string_view::npos) ? n : end + 1;
                text_from = i;
                // Pop the matching open element (lenient: pop to it if found in the stack;
                // ignore a stray close with no match).
                for (std::size_t k = open.size(); k-- > 1;) {
                    if (doc.nodes[static_cast<std::size_t>(open[k])].tag == name) {
                        open.resize(k);
                        break;
                    }
                }
                continue;
            }
            if (i + 1 < n && is_name_start(s[i + 1])) {  // start tag <name …>
                std::size_t j = i + 1;
                while (j < n && is_name_char(s[j])) ++j;
                Node el;
                el.is_element = true;
                el.tag = std::string(s.substr(i + 1, j - (i + 1)));
                i = j;
                bool self_close = false;
                parse_attrs(el, self_close);
                const int id = add_child(std::move(el));
                if (i < n && s[i] == '>') ++i;
                if (!self_close) open.push_back(id);
                text_from = i;
                continue;
            }
            // A '<' that begins nothing recognizable: treat it as text.
            ++i;
        }
        flush_text(text_from, n);
    }
};

const Node* node_at(const Document& doc, int id) {
    if (id < 0 || static_cast<std::size_t>(id) >= doc.nodes.size()) return nullptr;
    return &doc.nodes[static_cast<std::size_t>(id)];
}

void collect_text(const Document& doc, int id, std::string& out) {
    const Node* n = node_at(doc, id);
    if (!n) return;
    if (!n->is_element) { out += n->text; return; }
    for (const int c : n->children) collect_text(doc, c, out);
}

}  // namespace

Document parse(std::string_view xml) {
    Parser p;
    p.s = xml;
    p.run();
    return std::move(p.doc);
}

int root(const Document& doc) { return doc.root; }

bool is_element(const Document& doc, int id) {
    const Node* n = node_at(doc, id);
    return n && n->is_element;
}

std::string tag(const Document& doc, int id) {
    const Node* n = node_at(doc, id);
    return (n && n->is_element) ? n->tag : std::string();
}

std::string attr(const Document& doc, int id, std::string_view name) {
    const Node* n = node_at(doc, id);
    if (!n) return "";
    for (const Attr& a : n->attrs) if (a.name == name) return a.value;
    return "";
}

bool has_attr(const Document& doc, int id, std::string_view name) {
    const Node* n = node_at(doc, id);
    if (!n) return false;
    for (const Attr& a : n->attrs) if (a.name == name) return true;
    return false;
}

std::string text(const Document& doc, int id) {
    std::string out;
    collect_text(doc, id, out);
    return out;
}

std::vector<int> children(const Document& doc, int id) {
    const Node* n = node_at(doc, id);
    return n ? n->children : std::vector<int>{};
}

int find(const Document& doc, int id, std::string_view tag) {
    const Node* n = node_at(doc, id);
    if (!n) return -1;
    for (const int c : n->children) {
        const Node* cn = node_at(doc, c);
        if (cn && cn->is_element && cn->tag == tag) return c;
    }
    return -1;
}

std::vector<int> findall(const Document& doc, int id, std::string_view tag) {
    std::vector<int> out;
    const Node* n = node_at(doc, id);
    if (!n) return out;
    for (const int c : n->children) {
        const Node* cn = node_at(doc, c);
        if (cn && cn->is_element && cn->tag == tag) out.push_back(c);
    }
    return out;
}

std::vector<int> iter(const Document& doc, int id, std::string_view tag) {
    std::vector<int> out;
    if (!node_at(doc, id)) return out;
    std::vector<int> stack{id};
    while (!stack.empty()) {
        const int cur = stack.back();
        stack.pop_back();
        const Node* n = node_at(doc, cur);
        if (!n) continue;
        if (n->is_element && n->tag == tag) out.push_back(cur);
        // Push children in reverse so they pop in document order.
        for (std::size_t k = n->children.size(); k-- > 0;) stack.push_back(n->children[k]);
    }
    return out;
}

}  // namespace cheatah::parsers::xml
