#include "string.hpp"

#include <cctype>

namespace cheatah::string {

namespace {
char up(char c) { return static_cast<char>(std::toupper(static_cast<unsigned char>(c))); }
char lo(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
bool in_set(char c, std::string_view set) { return set.find(c) != std::string_view::npos; }
} // namespace

std::string upper(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = up(c);
    return r;
}
std::string lower(std::string_view s) {
    std::string r(s);
    for (char& c : r) c = lo(c);
    return r;
}
std::string capitalize(std::string_view s) {
    std::string r(s);
    if (!r.empty()) {
        r[0] = up(r[0]);
        for (std::size_t i = 1; i < r.size(); ++i) r[i] = lo(r[i]);
    }
    return r;
}
std::string title(std::string_view s) {
    std::string r(s);
    bool prev_alpha = false;
    for (char& c : r) {
        const bool alpha = std::isalpha(static_cast<unsigned char>(c)) != 0;
        if (alpha) c = prev_alpha ? lo(c) : up(c);
        prev_alpha = alpha;
    }
    return r;
}
std::string swapcase(std::string_view s) {
    std::string r(s);
    for (char& c : r) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::islower(u)) c = up(c);
        else if (std::isupper(u)) c = lo(c);
    }
    return r;
}

std::string lstrip(std::string_view s, std::string_view chars) {
    std::size_t i = 0;
    while (i < s.size() && in_set(s[i], chars)) ++i;
    return std::string(s.substr(i));
}
std::string rstrip(std::string_view s, std::string_view chars) {
    std::size_t n = s.size();
    while (n > 0 && in_set(s[n - 1], chars)) --n;
    return std::string(s.substr(0, n));
}
std::string strip(std::string_view s, std::string_view chars) { return rstrip(lstrip(s, chars), chars); }

bool startswith(std::string_view s, std::string_view prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}
bool endswith(std::string_view s, std::string_view suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}
bool contains(std::string_view s, std::string_view sub) { return s.find(sub) != std::string_view::npos; }

long find(std::string_view s, std::string_view sub) {
    const auto p = s.find(sub);
    return p == std::string_view::npos ? -1 : static_cast<long>(p);
}
long rfind(std::string_view s, std::string_view sub) {
    const auto p = s.rfind(sub);
    return p == std::string_view::npos ? -1 : static_cast<long>(p);
}
long count(std::string_view s, std::string_view sub) {
    if (sub.empty()) return static_cast<long>(s.size()) + 1;  // Python: len+1
    long n = 0;
    std::size_t pos = 0;
    while ((pos = s.find(sub, pos)) != std::string_view::npos) {
        ++n;
        pos += sub.size();
    }
    return n;
}

std::string replace(std::string_view s, std::string_view from, std::string_view to) {
    if (from.empty()) return std::string(s);
    std::string r;
    std::size_t prev = 0, pos;
    while ((pos = s.find(from, prev)) != std::string_view::npos) {
        r.append(s.substr(prev, pos - prev));
        r.append(to);
        prev = pos + from.size();
    }
    r.append(s.substr(prev));
    return r;
}

std::vector<std::string> split(std::string_view s, std::string_view sep) {
    std::vector<std::string> out;
    if (sep.empty()) {
        out.emplace_back(s);
        return out;
    }
    std::size_t prev = 0, pos;
    while ((pos = s.find(sep, prev)) != std::string_view::npos) {
        out.emplace_back(s.substr(prev, pos - prev));
        prev = pos + sep.size();
    }
    out.emplace_back(s.substr(prev));
    return out;
}
std::vector<std::string> split(std::string_view s) {
    std::vector<std::string> out;
    const std::size_t n = s.size();
    std::size_t i = 0;
    while (i < n) {
        while (i < n && in_set(s[i], whitespace)) ++i;
        if (i >= n) break;
        const std::size_t start = i;
        while (i < n && !in_set(s[i], whitespace)) ++i;
        out.emplace_back(s.substr(start, i - start));
    }
    return out;
}
std::vector<std::string> splitlines(std::string_view s) {
    std::vector<std::string> out;
    const std::size_t n = s.size();
    std::size_t i = 0, start = 0;
    while (i < n) {
        if (s[i] == '\n' || s[i] == '\r') {
            out.emplace_back(s.substr(start, i - start));
            if (s[i] == '\r' && i + 1 < n && s[i + 1] == '\n') ++i;
            ++i;
            start = i;
        } else {
            ++i;
        }
    }
    if (start < n) out.emplace_back(s.substr(start));
    return out;
}
std::string capwords(std::string_view s) {
    std::vector<std::string> words = split(s);
    for (std::string& w : words) w = capitalize(w);
    return join(" ", words);
}

std::string ljust(std::string_view s, std::size_t width, std::string_view fill) {
    const char f = fill.empty() ? ' ' : fill[0];
    std::string r(s);
    if (r.size() < width) r.append(width - r.size(), f);
    return r;
}
std::string rjust(std::string_view s, std::size_t width, std::string_view fill) {
    const char f = fill.empty() ? ' ' : fill[0];
    if (s.size() >= width) return std::string(s);
    return std::string(width - s.size(), f) + std::string(s);
}
std::string center(std::string_view s, std::size_t width, std::string_view fill) {
    const char f = fill.empty() ? ' ' : fill[0];
    if (s.size() >= width) return std::string(s);
    const std::size_t total = width - s.size();
    const std::size_t left = total / 2;
    return std::string(left, f) + std::string(s) + std::string(total - left, f);
}
std::string zfill(std::string_view s, std::size_t width) {
    if (s.size() >= width) return std::string(s);
    const std::size_t pad = width - s.size();
    if (!s.empty() && (s[0] == '+' || s[0] == '-')) {
        return std::string(1, s[0]) + std::string(pad, '0') + std::string(s.substr(1));
    }
    return std::string(pad, '0') + std::string(s);
}

namespace {
template <typename Pred>
bool all_of_nonempty(std::string_view s, Pred p) {
    if (s.empty()) return false;
    for (char c : s) {
        if (!p(static_cast<unsigned char>(c))) return false;
    }
    return true;
}
} // namespace

bool isdigit(std::string_view s) { return all_of_nonempty(s, [](unsigned char c) { return std::isdigit(c) != 0; }); }
bool isalpha(std::string_view s) { return all_of_nonempty(s, [](unsigned char c) { return std::isalpha(c) != 0; }); }
bool isalnum(std::string_view s) { return all_of_nonempty(s, [](unsigned char c) { return std::isalnum(c) != 0; }); }
bool isspace(std::string_view s) { return all_of_nonempty(s, [](unsigned char c) { return std::isspace(c) != 0; }); }

bool isupper(std::string_view s) {
    bool has = false;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::islower(u)) return false;
        if (std::isupper(u)) has = true;
    }
    return has;
}
bool islower(std::string_view s) {
    bool has = false;
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isupper(u)) return false;
        if (std::islower(u)) has = true;
    }
    return has;
}

} // namespace cheatah::string
