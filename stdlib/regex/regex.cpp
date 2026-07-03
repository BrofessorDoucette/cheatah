// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "regex.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// A from-scratch, linear-time regex engine: the pattern compiles to a Thompson NFA and runs as
// a lazy DFA (RE2-style). Matching touches only the input bytes (via string_view) and integer
// program-counters — it NEVER allocates an intermediate string. The compiled program and its
// DFA cache live in one heap-owned `Dfa` that a `Pattern` shares, so reuse is fast.
namespace cheatah::regex {

namespace {

struct Inst {
    enum Op : std::uint8_t { Byte, Split, Jmp, Match } op = Match;
    std::uint64_t cls[4] = {0, 0, 0, 0};  // 256-bit accept set for Byte
    int x = 0, y = 0;                     // targets
};

// ---- 256-bit byte class helpers ------------------------------------------------------
void set_bit(std::uint64_t cls[4], unsigned char b) { cls[b >> 6] |= (std::uint64_t{1} << (b & 63)); }
bool has_bit(const std::uint64_t cls[4], unsigned char b) { return (cls[b >> 6] >> (b & 63)) & 1u; }
void set_range(std::uint64_t cls[4], unsigned lo, unsigned hi) {
    for (unsigned c = lo; c <= hi; ++c) set_bit(cls, static_cast<unsigned char>(c));
}
void negate(std::uint64_t cls[4]) { for (int i = 0; i < 4; ++i) cls[i] = ~cls[i]; }

// ---- the compiler: regex source -> NFA program (Thompson construction) ----------------
struct Hole { int inst; int field; };          // a dangling exit: (instruction, 0=x|1=y)
struct Frag { int start; std::vector<Hole> holes; };

struct Compiler {
    std::string_view src;
    std::size_t i = 0;
    std::vector<Inst> prog;
    std::string err;

    bool eof() const { return i >= src.size(); }
    char peek() const { return i < src.size() ? src[i] : '\0'; }
    char get() { return src[i++]; }
    int emit(const Inst& in) { prog.push_back(in); return static_cast<int>(prog.size()) - 1; }
    void patch(const std::vector<Hole>& holes, int target) {
        for (const Hole& h : holes) (h.field == 0 ? prog[h.inst].x : prog[h.inst].y) = target;
    }

    Frag byte_frag(const std::uint64_t cls[4]) {
        Inst in; in.op = Inst::Byte;
        for (int k = 0; k < 4; ++k) in.cls[k] = cls[k];
        int s = emit(in);
        return Frag{s, {{s, 0}}};
    }

    bool escape_class(char c, std::uint64_t cls[4]) {
        switch (c) {
            case 'd': set_range(cls, '0', '9'); return true;
            case 'w': set_range(cls, 'a', 'z'); set_range(cls, 'A', 'Z');
                      set_range(cls, '0', '9'); set_bit(cls, '_'); return true;
            case 's': set_bit(cls, ' '); set_bit(cls, '\t'); set_bit(cls, '\n');
                      set_bit(cls, '\r'); set_bit(cls, '\f'); set_bit(cls, '\v'); return true;
            case 'D': case 'W': case 'S': {
                std::uint64_t base[4] = {0, 0, 0, 0};
                escape_class(static_cast<char>(c + 32), base);
                negate(base);
                for (int k = 0; k < 4; ++k) cls[k] |= base[k];
                return true;
            }
            default: return false;
        }
    }

    Frag parse_alt() {
        Frag left = parse_concat();
        while (err.empty() && peek() == '|') {
            get();
            Frag right = parse_concat();
            if (!err.empty()) return left;
            Inst sp; sp.op = Inst::Split; sp.x = left.start; sp.y = right.start;
            int s = emit(sp);
            Frag f; f.start = s; f.holes = left.holes;
            f.holes.insert(f.holes.end(), right.holes.begin(), right.holes.end());
            left = f;
        }
        return left;
    }

    Frag parse_concat() {
        Frag acc; acc.start = -1;
        while (err.empty() && !eof() && peek() != '|' && peek() != ')') {
            Frag r = parse_repeat();
            if (!err.empty()) return acc;
            if (acc.start == -1) acc = r;
            else { patch(acc.holes, r.start); acc.holes = r.holes; }
        }
        if (acc.start == -1) {  // empty -> an epsilon Jmp pass-through
            Inst j; j.op = Inst::Jmp; int s = emit(j);
            acc.start = s; acc.holes = {{s, 0}};
        }
        return acc;
    }

    Frag parse_repeat() {
        Frag a = parse_atom();
        while (err.empty() && (peek() == '*' || peek() == '+' || peek() == '?')) {
            char q = get();
            Inst sp; sp.op = Inst::Split;
            if (q == '*') {
                sp.x = a.start; int s = emit(sp);
                patch(a.holes, s);
                a = Frag{s, {{s, 1}}};
            } else if (q == '+') {
                sp.x = a.start; int s = emit(sp);
                patch(a.holes, s);
                a = Frag{a.start, {{s, 1}}};
            } else {  // '?'
                sp.x = a.start; int s = emit(sp);
                Frag f; f.start = s; f.holes = a.holes; f.holes.push_back({s, 1});
                a = f;
            }
        }
        return a;
    }

    Frag parse_atom() {
        char c = peek();
        if (c == '(') {
            get();
            Frag inner = parse_alt();
            if (peek() != ')') { err = "unbalanced '('"; return inner; }
            get();
            return inner;
        }
        if (c == '[') return parse_class();
        if (c == '.') {  // any byte except newline
            get();
            std::uint64_t cls[4] = {~std::uint64_t{0}, ~std::uint64_t{0}, ~std::uint64_t{0}, ~std::uint64_t{0}};
            cls['\n' >> 6] &= ~(std::uint64_t{1} << ('\n' & 63));
            return byte_frag(cls);
        }
        if (c == '\\') {
            get();
            if (eof()) { err = "trailing backslash"; return {}; }
            char e = get();
            std::uint64_t cls[4] = {0, 0, 0, 0};
            if (escape_class(e, cls)) return byte_frag(cls);
            set_bit(cls, static_cast<unsigned char>(e));  // escaped literal
            return byte_frag(cls);
        }
        if (c == ')' || c == '|' || c == '*' || c == '+' || c == '?') { err = "unexpected metacharacter"; return {}; }
        get();
        std::uint64_t cls[4] = {0, 0, 0, 0};
        set_bit(cls, static_cast<unsigned char>(c));
        return byte_frag(cls);
    }

    Frag parse_class() {
        get();  // '['
        bool neg = false;
        if (peek() == '^') { get(); neg = true; }
        std::uint64_t cls[4] = {0, 0, 0, 0};
        bool first = true;
        while (!eof() && (peek() != ']' || first)) {
            first = false;
            char c = get();
            if (c == '\\' && !eof()) {
                char e = get();
                std::uint64_t sub[4] = {0, 0, 0, 0};
                if (escape_class(e, sub)) { for (int k = 0; k < 4; ++k) cls[k] |= sub[k]; continue; }
                c = e;
            }
            if (peek() == '-' && i + 1 < src.size() && src[i + 1] != ']') {
                get();
                char hi = get();
                set_range(cls, static_cast<unsigned char>(c), static_cast<unsigned char>(hi));
            } else {
                set_bit(cls, static_cast<unsigned char>(c));
            }
        }
        if (peek() != ']') { err = "unbalanced '['"; return {}; }
        get();
        if (neg) { negate(cls); cls['\n' >> 6] &= ~(std::uint64_t{1} << ('\n' & 63)); }  // negated class excludes newline
        return byte_frag(cls);
    }
};

}  // namespace

/// The compiled Thompson-NFA program plus its lazy-DFA cache — an internal implementation type
/// (@ref Pattern holds one by `shared_ptr`; not part of the public surface).
struct Dfa {
    std::vector<Inst> prog;          ///< the compiled Thompson-NFA program.
    int start_unanchored = 0;        ///< program entry pc for an unanchored start.
    int start_anchored = 0;          ///< program entry pc for an anchored start.
    bool anchored_start = false;     ///< whether the pattern is anchored at the start (`^`).
    bool anchored_end = false;       ///< whether the pattern is anchored at the end (`$`).

    std::unordered_map<std::string, int> intern;  ///< canonical (sorted) pc-set key -> DFA state id.
    std::vector<std::vector<int>> pcs;             ///< per-state pc set.
    std::vector<char> accepts;                     ///< per-state "accepting" flag.
    std::vector<int> tflat;   ///< flat transition table (`tflat[state*256+byte]`; -1 = uncomputed).

    std::uint64_t first[4] = {0, 0, 0, 0};  ///< bitset of bytes an anchored match can START with.
    int first_count = 0;             ///< popcount of `first` (0 = unknown / matches empty).
    unsigned char first_byte = 0;    ///< the single required first byte when `first_count == 1`.
    bool matches_empty = false;      ///< the pattern can match the empty string at any position.

    /// Add the epsilon-closure of @p pc to @p out. @param pc start program counter.
    /// @param out accumulates the reachable Byte/Match pcs. @param seen per-pc visited flags.
    void add_closure(int pc, std::vector<int>& out, std::vector<char>& seen) const {
        if (pc < 0 || seen[pc]) return;
        seen[pc] = 1;
        const Inst& in = prog[pc];
        if (in.op == Inst::Jmp) add_closure(in.x, out, seen);
        else if (in.op == Inst::Split) { add_closure(in.x, out, seen); add_closure(in.y, out, seen); }
        else out.push_back(pc);  // Byte or Match — a real state
    }

    /// Intern a pc @p set into a canonical DFA state (creating it if new).
    /// @param set the pc set for the state. @return the state id.
    int intern_state(std::vector<int> set) {
        std::sort(set.begin(), set.end());
        set.erase(std::unique(set.begin(), set.end()), set.end());
        std::string key(reinterpret_cast<const char*>(set.data()), set.size() * sizeof(int));
        auto it = intern.find(key);
        if (it != intern.end()) return it->second;
        int id = static_cast<int>(pcs.size());
        bool acc = false;
        for (int pc : set) if (prog[pc].op == Inst::Match) acc = true;
        intern.emplace(std::move(key), id);
        pcs.push_back(std::move(set));
        accepts.push_back(acc ? 1 : 0);
        tflat.resize(tflat.size() + 256, -1);
        return id;
    }

    /// The start DFA state for program entry @p entry. @param entry the program entry pc.
    /// @return the interned start state id.
    int start_state(int entry) {
        std::vector<int> out;
        std::vector<char> seen(prog.size(), 0);
        add_closure(entry, out, seen);
        return intern_state(std::move(out));
    }

    /// Transition from @p state on input byte @p b, lazily filling the cache. @param state the current
    /// DFA state id. @param b the input byte. @return the next state id.
    int step(int state, unsigned char b) {
        int cached = tflat[static_cast<std::size_t>(state) * 256 + b];
        if (cached != -1) return cached;
        std::vector<int> out;
        std::vector<char> seen(prog.size(), 0);
        for (int pc : pcs[state]) {
            const Inst& in = prog[pc];
            if (in.op == Inst::Byte && has_bit(in.cls, b)) add_closure(in.x, out, seen);
        }
        int next = intern_state(std::move(out));  // may grow tflat -> index AFTER, not before
        tflat[static_cast<std::size_t>(state) * 256 + b] = next;
        return next;
    }
};

Pattern compile(std::string_view pattern) {
    Pattern out;
    auto dfa = std::make_shared<Dfa>();
    if (!pattern.empty() && pattern.front() == '^') { dfa->anchored_start = true; pattern.remove_prefix(1); }
    if (!pattern.empty() && pattern.back() == '$') { dfa->anchored_end = true; pattern.remove_suffix(1); }

    Compiler c;
    // Slots 0..1 = the unanchored `.*?` prefix, so an unanchored search begins a match at ANY
    // position with no per-position work: 0: Split(body, 1); 1: Byte(any) -> 0.
    c.emit(Inst{});
    Inst anybyte; anybyte.op = Inst::Byte;
    for (int k = 0; k < 4; ++k) anybyte.cls[k] = ~std::uint64_t{0};
    anybyte.x = 0;
    c.emit(anybyte);
    c.src = pattern;

    Frag f = c.parse_alt();
    if (c.err.empty() && c.i != c.src.size()) c.err = "unexpected trailing input";
    if (!c.err.empty()) { out.error = c.err; return out; }

    Inst m; m.op = Inst::Match;
    int match_pc = c.emit(m);
    c.patch(f.holes, match_pc);
    c.prog[0].op = Inst::Split;
    c.prog[0].x = f.start;   // anchored entry: the pattern body
    c.prog[0].y = 1;

    dfa->prog = std::move(c.prog);
    dfa->start_unanchored = 0;
    dfa->start_anchored = f.start;

    // The bytes an anchored match can begin with (its start-state closure's Byte classes) — the
    // fast prefix scan uses this to skip over input that can't begin a match.
    {
        std::vector<int> sc;
        std::vector<char> seen(dfa->prog.size(), 0);
        dfa->add_closure(dfa->start_anchored, sc, seen);
        for (int pc : sc) {
            const Inst& in = dfa->prog[pc];
            if (in.op == Inst::Byte) for (int k = 0; k < 4; ++k) dfa->first[k] |= in.cls[k];
            if (in.op == Inst::Match) dfa->matches_empty = true;  // accepts the empty string
        }
        int cnt = 0, single = -1;
        for (int b = 0; b < 256; ++b) if (has_bit(dfa->first, static_cast<unsigned char>(b))) { ++cnt; single = b; }
        dfa->first_count = cnt;
        if (cnt == 1) dfa->first_byte = static_cast<unsigned char>(single);
    }

    out.impl = std::move(dfa);
    out.ok = true;
    return out;
}

namespace {
// Run the anchored DFA from byte offset `from`; true if it reaches an accepting state (that
// also satisfies the end-anchor, if any). Touches only `text`'s bytes.
bool run_anchored(Dfa& d, std::string_view text, std::size_t from, bool need_end, int start_state) {
    int state = start_state;
    for (std::size_t p = from;; ++p) {
        if (d.accepts[state] && (!need_end || p == text.size())) return true;
        if (p == text.size()) return false;
        state = d.step(state, static_cast<unsigned char>(text[p]));
        if (d.pcs[state].empty() && !d.accepts[state]) return false;  // dead — cannot extend
    }
}

bool run(const Pattern& re, std::string_view text, bool whole) {
    if (!re.ok || !re.impl) return false;
    Dfa& d = *re.impl;
    const bool need_end = whole || d.anchored_end;
    const int astart = d.start_state(d.start_anchored);

    if (whole || d.anchored_start)                       // pinned to the start
        return run_anchored(d, text, 0, need_end, astart);
    if (d.matches_empty && !need_end) return true;       // an empty match sits at position 0
    if (d.first_count == 0)                              // no first-byte info -> full DFA pass
        return run_anchored(d, text, 0, need_end, d.start_state(d.start_unanchored));

    // Unanchored: skip to each candidate start byte with a fast scan, run the anchored DFA there.
    const char* base = text.data();
    const std::size_t n = text.size();
    for (std::size_t i = 0; i < n;) {
        if (d.first_count == 1) {
            const void* hit = std::memchr(base + i, d.first_byte, n - i);
            if (!hit) break;
            i = static_cast<std::size_t>(static_cast<const char*>(hit) - base);
        } else {
            while (i < n && !has_bit(d.first, static_cast<unsigned char>(base[i]))) ++i;
            if (i >= n) break;
        }
        if (run_anchored(d, text, i, need_end, astart)) return true;
        ++i;
    }
    return false;
}
}  // namespace

bool search(const Pattern& re, std::string_view text) { return run(re, text, false); }
bool full_match(const Pattern& re, std::string_view text) { return run(re, text, true); }

Match find(const Pattern& re, std::string_view text) {
    Match r;
    if (!re.ok || !re.impl) return r;
    Dfa& d = *re.impl;
    const std::size_t last_start = d.anchored_start ? 0 : text.size();
    for (std::size_t i = 0; i <= last_start; ++i) {
        int state = d.start_state(d.start_anchored);
        long best_end = -1;
        for (std::size_t p = i;; ++p) {
            if (d.accepts[state] && (!d.anchored_end || p == text.size())) best_end = static_cast<long>(p);
            if (p == text.size()) break;
            state = d.step(state, static_cast<unsigned char>(text[p]));
            if (d.pcs[state].empty() && !d.accepts[state]) break;  // dead — no match can extend
        }
        if (best_end >= 0) {
            r.matched = true;
            r.begin = i;
            r.end = static_cast<std::size_t>(best_end);
            r.text.assign(text.data() + i, r.end - i);   // OWN a copy of the matched bytes
            return r;
        }
    }
    return r;
}

}  // namespace cheatah::regex
