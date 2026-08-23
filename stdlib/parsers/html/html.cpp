// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "html.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cheatah::parsers::html {

namespace {

// ASCII-lowercase a byte (tag/attr names; HTML is ASCII-case-insensitive there).
char lower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

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

// A focused table of the common named entities -> Unicode code point. Not the
// full HTML5 set; unknown names are left verbatim by unescape().
const std::unordered_map<std::string_view, std::uint32_t>& named_entities() {
    static const std::unordered_map<std::string_view, std::uint32_t> kEntities = {
        {"amp", 0x26},    {"lt", 0x3C},      {"gt", 0x3E},      {"quot", 0x22},
        {"apos", 0x27},   {"nbsp", 0xA0},    {"copy", 0xA9},    {"reg", 0xAE},
        {"trade", 0x2122},{"hellip", 0x2026},{"mdash", 0x2014}, {"ndash", 0x2013},
        {"lsquo", 0x2018},{"rsquo", 0x2019}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
        {"deg", 0xB0},    {"plusmn", 0xB1},  {"times", 0xD7},   {"divide", 0xF7},
        {"frac12", 0xBD}, {"frac14", 0xBC},  {"frac34", 0xBE},  {"euro", 0x20AC},
        {"pound", 0xA3},  {"cent", 0xA2},    {"yen", 0xA5},     {"sect", 0xA7},
        {"para", 0xB6},   {"middot", 0xB7},  {"laquo", 0xAB},   {"raquo", 0xBB},
        {"bull", 0x2022}, {"dagger", 0x2020},{"permil", 0x2030},{"prime", 0x2032},
        {"eacute", 0xE9}, {"egrave", 0xE8},  {"agrave", 0xE0},  {"ccedil", 0xE7},
        {"auml", 0xE4},   {"ouml", 0xF6},    {"uuml", 0xFC},    {"szlig", 0xDF},
        {"aelig", 0xE6},  {"oslash", 0xF8},  {"ntilde", 0xF1},  {"micro", 0xB5},
    };
    return kEntities;
}

// Decode the reference whose body (between '&' and the optional ';') is `body`.
// Returns true and appends to `out` on success; false leaves it to the caller.
bool decode_reference(std::string_view body, std::string& out) {
    if (body.empty()) return false;
    if (body.front() == '#') {  // numeric: &#169; or &#xA9;
        std::uint32_t cp = 0;
        bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
        std::size_t i = hex ? 2 : 1;
        if (i >= body.size()) return false;
        for (; i < body.size(); ++i) {
            const char c = body[i];
            std::uint32_t digit = 0;
            if (c >= '0' && c <= '9') {
                digit = static_cast<std::uint32_t>(c - '0');
            } else if (hex && c >= 'a' && c <= 'f') {
                digit = static_cast<std::uint32_t>(c - 'a' + 10);
            } else if (hex && c >= 'A' && c <= 'F') {
                digit = static_cast<std::uint32_t>(c - 'A' + 10);
            } else {
                return false;
            }
            cp = cp * (hex ? 16 : 10) + digit;
            if (cp > 0x10FFFF) return false;  // beyond Unicode range
        }
        if (cp == 0) return false;
        append_utf8(out, cp);
        return true;
    }
    const auto& table = named_entities();
    const auto it = table.find(body);
    if (it == table.end()) return false;
    append_utf8(out, it->second);
    return true;
}

bool is_name_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':';
}
bool is_name_char(char c) {
    return is_name_start(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}
bool is_space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }

std::string lower_str(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) out.push_back(lower(c));
    return out;
}

// Case-insensitive check that s[at..] begins with `lit` (lit is lowercase).
bool matches_ci(std::string_view s, std::size_t at, std::string_view lit) {
    if (at + lit.size() > s.size()) return false;
    for (std::size_t k = 0; k < lit.size(); ++k) {
        if (lower(s[at + k]) != lit[k]) return false;
    }
    return true;
}

}  // namespace

std::string escape(std::string_view s, bool quote) {
    std::string out;
    out.reserve(s.size());
    for (const char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += quote ? "&quot;" : "\""; break;
            case '\'': out += quote ? "&#x27;" : "'"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

std::string unescape(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size();) {
        if (s[i] != '&') {
            out.push_back(s[i++]);
            continue;
        }
        // Find the terminating ';' within a sane window (entity names are short).
        const std::size_t semi = s.find(';', i + 1);
        const std::size_t limit = i + 1 + 32;
        if (semi != std::string_view::npos && semi <= limit && semi > i + 1 &&
            decode_reference(s.substr(i + 1, semi - i - 1), out)) {
            i = semi + 1;
        } else {
            out.push_back(s[i++]);  // a bare '&' or unknown reference: keep verbatim
        }
    }
    return out;
}

std::vector<Token> parse(std::string_view html) {
    std::vector<Token> tokens;
    const std::size_t n = html.size();
    std::size_t i = 0;
    std::string text;  // accumulates a run of character data

    auto flush_text = [&] {
        if (text.empty()) return;
        tokens.push_back({"data", "", unescape(text), {}});
        text.clear();
    };

    while (i < n) {
        if (html[i] != '<') {
            text.push_back(html[i++]);
            continue;
        }
        // Something starting with '<'. Decide what it is.
        if (matches_ci(html, i, "<!--")) {  // comment
            flush_text();
            const std::size_t start = i + 4;
            std::size_t end = html.find("-->", start);
            const std::size_t stop = (end == std::string_view::npos) ? n : end;
            tokens.push_back({"comment", "", std::string(html.substr(start, stop - start)), {}});
            i = (end == std::string_view::npos) ? n : end + 3;
            continue;
        }
        if (i + 1 < n && html[i + 1] == '!') {  // declaration <!DOCTYPE ...>
            flush_text();
            const std::size_t start = i + 2;
            std::size_t end = html.find('>', start);
            const std::size_t stop = (end == std::string_view::npos) ? n : end;
            tokens.push_back({"decl", "", std::string(html.substr(start, stop - start)), {}});
            i = (end == std::string_view::npos) ? n : end + 1;
            continue;
        }
        if (i + 1 < n && html[i + 1] == '?') {  // processing instruction <? ... >
            flush_text();
            const std::size_t start = i + 2;
            std::size_t end = html.find('>', start);
            const std::size_t stop = (end == std::string_view::npos) ? n : end;
            tokens.push_back({"pi", "", std::string(html.substr(start, stop - start)), {}});
            i = (end == std::string_view::npos) ? n : end + 1;
            continue;
        }
        if (i + 1 < n && html[i + 1] == '/') {  // end tag </name>
            const std::size_t name_start = i + 2;
            std::size_t j = name_start;
            while (j < n && is_name_char(html[j])) ++j;
            if (j == name_start && (j >= n || !is_name_start(html[j]))) {
                // not a real name (e.g. "</ "): only a tag if name present
            }
            if (j > name_start) {
                flush_text();
                std::size_t end = html.find('>', j);
                tokens.push_back({"endtag", lower_str(html.substr(name_start, j - name_start)), "", {}});
                i = (end == std::string_view::npos) ? n : end + 1;
                continue;
            }
            // malformed: treat '<' as data
            text.push_back(html[i++]);
            continue;
        }
        if (i + 1 < n && is_name_start(html[i + 1])) {  // start tag <name ...>
            flush_text();
            std::size_t j = i + 1;
            while (j < n && is_name_char(html[j])) ++j;
            const std::string tag = lower_str(html.substr(i + 1, j - (i + 1)));

            std::vector<Attr> attrs;
            // Parse attributes until '>' or '/>' or end-of-input.
            while (j < n && html[j] != '>') {
                while (j < n && is_space(html[j])) ++j;
                if (j < n && html[j] == '/') { ++j; continue; }   // self-close marker
                if (j >= n || html[j] == '>') break;
                // attribute name
                const std::size_t an = j;
                while (j < n && !is_space(html[j]) && html[j] != '=' && html[j] != '>' &&
                       html[j] != '/') {
                    ++j;
                }
                if (j == an) { ++j; continue; }  // stray char, skip
                std::string name = lower_str(html.substr(an, j - an));
                std::string value;
                while (j < n && is_space(html[j])) ++j;
                if (j < n && html[j] == '=') {
                    ++j;
                    while (j < n && is_space(html[j])) ++j;
                    if (j < n && (html[j] == '"' || html[j] == '\'')) {
                        const char q = html[j++];
                        const std::size_t vs = j;
                        while (j < n && html[j] != q) ++j;
                        value = unescape(html.substr(vs, j - vs));
                        if (j < n) ++j;  // closing quote
                    } else {
                        const std::size_t vs = j;
                        while (j < n && !is_space(html[j]) && html[j] != '>') ++j;
                        value = unescape(html.substr(vs, j - vs));
                    }
                }
                attrs.push_back({std::move(name), std::move(value)});
            }
            // Detect self-closing: last non-space before '>' was '/'.
            bool self_close = false;
            if (j < n && html[j] == '>') {
                std::size_t k = j;
                while (k > i && is_space(html[k - 1])) --k;
                if (k > i && html[k - 1] == '/') self_close = true;
            }
            const std::size_t after = (j < n) ? j + 1 : n;

            tokens.push_back({self_close ? "startendtag" : "starttag", tag, "", std::move(attrs)});

            // Raw-text elements: emit their body verbatim, then the end tag.
            if (!self_close && (tag == "script" || tag == "style")) {
                const std::string close = "</" + tag;
                std::size_t k = after;
                while (k < n) {
                    if (html[k] == '<' && matches_ci(html, k, close)) break;
                    ++k;
                }
                if (k > after) {
                    tokens.push_back({"data", "", std::string(html.substr(after, k - after)), {}});
                }
                if (k < n) {  // consume the matching close tag
                    std::size_t end = html.find('>', k);
                    tokens.push_back({"endtag", tag, "", {}});
                    i = (end == std::string_view::npos) ? n : end + 1;
                } else {
                    i = n;
                }
                continue;
            }
            i = after;
            continue;
        }
        // A '<' that starts nothing recognizable -> literal data.
        text.push_back(html[i++]);
    }
    flush_text();
    return tokens;
}

std::string get_attr(const Token& t, std::string_view name) {
    const std::string key = lower_str(name);
    for (const Attr& a : t.attrs) {
        if (a.name == key) return a.value;
    }
    return "";
}

bool has_attr(const Token& t, std::string_view name) {
    const std::string key = lower_str(name);
    return std::ranges::any_of(t.attrs, [&key](const Attr& a) { return a.name == key; });
}

}  // namespace cheatah::parsers::html
