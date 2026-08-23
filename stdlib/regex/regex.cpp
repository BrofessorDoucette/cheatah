// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include <stdexcept>
#include "regex.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>
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
bool has_bit(const std::uint64_t cls[4], unsigned char b) { return ((cls[b >> 6] >> (b & 63)) & 1u) != 0u; }
void set_range(std::uint64_t cls[4], unsigned lo, unsigned hi) {
    for (unsigned c = lo; c <= hi; ++c) set_bit(cls, static_cast<unsigned char>(c));
}
void negate(std::uint64_t cls[4]) { for (int i = 0; i < 4; ++i) cls[i] = ~cls[i]; }

// ---- the compiler: regex source -> NFA program (Thompson construction) ----------------
struct Hole { int inst; int field; };          // a dangling exit: (instruction, 0=x|1=y)
struct Frag { int start{}; std::vector<Hole> holes; };

// Maximum group-nesting depth accepted by the recursive-descent parser. `(((…` recurses one
// parse_atom→parse_alt cycle per '(', so an unbounded pattern would overflow the C++ stack before
// it could even report "unbalanced '('". 1000 is far beyond any real pattern. Patterns are
// developer-supplied today, but a program that compiles a regex from untrusted text (e.g. a
// user-supplied filter) would otherwise crash on a crafted pattern.
inline constexpr int kMaxParseDepth = 1000;

// Maximum accepted pattern length. compile() spends O(m) memory — roughly 40 bytes of NFA
// program per pattern byte — so an unbounded hostile pattern is a ~40x memory-amplification
// DoS that the nesting cap above does not cover (it bounds depth, not breadth). 64 KiB is
// orders of magnitude beyond any real pattern; longer input is rejected as malformed.
inline constexpr std::size_t kMaxPatternLength = std::size_t{64} * 1024;

struct Compiler {
    std::string_view src;
    std::size_t i = 0;
    std::vector<Inst> prog;
    std::string err;
    int depth = 0;      // current group-nesting recursion depth (see kMaxParseDepth)
    bool reversed = false;  // emit the byte-reversed program: rev(A·B) = rev(B)·rev(A)

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
            else if (!reversed) { patch(acc.holes, r.start); acc.holes = r.holes; }
            else { patch(r.holes, acc.start); acc.start = r.start; }  // concatenate backwards
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
            if (++depth > kMaxParseDepth) { err = "pattern nested too deeply"; return {}; }
            Frag inner = parse_alt();
            --depth;
            if (!err.empty()) return inner;  // keep the inner error (depth cap, bad metachar, …)
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
    std::vector<int> tflat;  ///< flat transition table: @ref kRowInts ints per state (256
                             ///< packed next-state entries + the accept flag in slot
                             ///< @ref kFlagSlot); -1 = uncomputed.

    std::uint64_t first[4] = {0, 0, 0, 0};  ///< bitset of bytes an anchored match can START with.
    int first_count = 0;             ///< popcount of `first` (0 = unknown / matches empty).
    unsigned char first_byte = 0;    ///< the single required first byte when `first_count == 1`.
    bool matches_empty = false;      ///< the pattern can match the empty string at any position.
    std::array<unsigned char, 256> in_first{};  ///< byte-indexed `first` membership (the skip
                                                ///< LUT), built on first use — see `in_first_ready`.
    bool in_first_ready = false;  ///< whether `in_first` has been expanded from `first` yet.
    /// Bytes whose transition FROM the unanchored start returns the unanchored start itself —
    /// provably free to skip in an existence scan (the tracked start-set is unchanged), even
    /// when they're in @ref first (`(a|a)*c` over "aaaa": 'a' loops the start state forever).
    /// Learned lazily as those transitions are first computed; unknown bytes just step.
    std::array<unsigned char, 256> ustart_self{};
    std::string lit;   ///< the literal chain the pattern must start with (empty when shorter than 2).
    int astart_id = -1;  ///< interned DFA id of the anchored start state (-1 until first use).
    int ustart_id = -1;  ///< interned DFA id of the unanchored start state (-1 until first use).
    std::unique_ptr<Dfa> rev;  ///< the reversed program (built only for `$`-only-anchored patterns).

    /// Hot-loop state values ARE their row's BYTE OFFSET into the flat transition table:
    /// each state's row is @ref kRowInts ints — 256 transitions plus, in slot 256, the
    /// state's accept flag. A transition is one `[table_bytes + state + byte*4]` load (the
    /// byte-side arithmetic is off the dependent chain, so the carried latency is a single
    /// simple load — RE2's chain, without its bytemap indirection), and the accept test is
    /// an independent load of the row's flag slot that never extends the chain. The dead
    /// state is id 0, so its offset is 0, its all-zero row self-loops, and the dead test
    /// stays `state == 0`.
    static constexpr int kRowInts = 257;
    /// One row's size in bytes — the stride between consecutive packed state values.
    static constexpr int kRowBytes = kRowInts * static_cast<int>(sizeof(int));
    /// The in-row index of the accept flag (right after the 256 transition entries).
    static constexpr int kFlagSlot = 256;

    /// @param id a raw interned state id. @return the state's packed value: its row's byte
    ///   offset into @ref tflat (the accept flag lives inside the row, not in the value).
    /// @complexity O(1). @alloc none. @test Regex.FullMatchIsAnchoredBothEnds
    static int pack(int id) { return id * kRowBytes; }

    /// The PACKED start id for program entry @p entry, cached (packed) in @p slot so the
    /// closure walk, its allocations, AND the accept-flag pack happen once per pattern, not
    /// once per call. The first call also seeds state id 0 as the canonical DEAD state
    /// (empty pc set, self-looping all-zero transition row, no accept): every empty closure
    /// interns to it for free, and the hot loops' dead test is one compare against 0.
    /// @param entry the program entry pc. @param slot the cache slot (`astart_id`/`ustart_id`).
    /// @return the packed start state id.
    /// @complexity O(program size) on the first call; O(1) after.
    /// @alloc first call only: the id-0 bookkeeping + closure scratch + interned state.
    /// @test Regex.PatternIsReusableAndCheapToCopy
    int cached_start(int entry, int& slot) {
        if (slot >= 0) return slot;  // the hot path — everything below runs once per pattern
        if (pcs.empty()) {
            intern.emplace(std::string(), 0);
            pcs.emplace_back();
            accepts.push_back(0);
            tflat.assign(static_cast<std::size_t>(kRowInts), 0);
        }
        slot = pack(start_state(entry));
        return slot;
    }

    /// Add the epsilon-closure of @p pc to @p out. @param pc start program counter.
    /// @param out accumulates the reachable Byte/Match pcs. @param seen per-pc visited flags.
    /// @complexity O(program size) — the worklist visits each pc at most once (@p seen).
    /// @alloc the local worklist vector (the caller owns @p out / @p seen).
    /// @test RegexE2E.SearchPresentAbsent
    void add_closure(int pc, std::vector<int>& out, std::vector<char>& seen) const {
        // Iterative worklist rather than recursion: a long epsilon chain (`a?`×N, nested
        // alternations) makes the closure O(pattern length) deep, which as recursion overflowed the
        // C++ stack on a valid pattern. The membership set `out` is sorted+de-duplicated by the
        // caller (intern_state) and only its CONTENTS matter (state identity, accept flag, first-byte
        // OR), so the visitation order here is irrelevant — an explicit stack is equivalent.
        std::vector<int> work;
        work.push_back(pc);
        while (!work.empty()) {
            const int p = work.back();
            work.pop_back();
            if (p < 0 || seen[p]) continue;
            seen[p] = 1;
            const Inst& in = prog[p];
            if (in.op == Inst::Jmp) work.push_back(in.x);
            else if (in.op == Inst::Split) { work.push_back(in.x); work.push_back(in.y); }
            else out.push_back(p);  // Byte or Match — a real state
        }
    }

    /// Hard ceiling on distinct lazy-DFA states. Subset construction can in theory create up to
    /// 2^(NFA states) DFA states — a ~30-`.` pattern is enough — each costing a @ref kRowInts-int
    /// transition row (~1 KiB), so an uncapped cache is a memory-exhaustion DoS on a crafted
    /// pattern (RE2, the model, bounds its cache and falls back). Matching TIME stays linear; this
    /// bounds only MEMORY. 100k states (~100 MiB ceiling) is orders of magnitude beyond any real
    /// pattern; exceeding it throws rather than OOMs, so a caller can catch a pathological pattern.
    static constexpr std::size_t kMaxStates = 100000;

    /// Intern a pc @p set into a canonical DFA state (creating it if new).
    /// @param set the pc set for the state. @return the state id.
    /// @complexity O(|set| log |set|) for the canonical sort, then an amortized-O(|set|)
    ///   hash lookup; throws past @ref kMaxStates instead of exhausting memory.
    /// @alloc the canonical key string; a NEW state also stores its pc set and grows the
    ///   flat transition table by one @ref kRowInts-entry row (256 transitions + the accept
    ///   flag slot). An existing state allocates the key only.
    /// @test RegexE2E.ReDoSNestedQuantifierReturnsFalseFast
    int intern_state(std::vector<int> set) {
        std::sort(set.begin(), set.end());
        set.erase(std::unique(set.begin(), set.end()), set.end());
        std::string key(reinterpret_cast<const char*>(set.data()), set.size() * sizeof(int));
        auto it = intern.find(key);
        if (it != intern.end()) return it->second;
        if (pcs.size() >= kMaxStates)
            throw std::runtime_error("regex: DFA state budget exceeded (pathological pattern)");
        int id = static_cast<int>(pcs.size());
        bool acc = false;
        for (int pc : set) if (prog[pc].op == Inst::Match) acc = true;
        intern.emplace(std::move(key), id);
        pcs.push_back(std::move(set));
        accepts.push_back(acc ? 1 : 0);
        tflat.resize(tflat.size() + kRowInts, -1);
        tflat[static_cast<std::size_t>(id) * kRowInts + kFlagSlot] = acc ? 1 : 0;
        return id;
    }

    /// The start DFA state for program entry @p entry. @param entry the program entry pc.
    /// @return the interned start state id.
    /// @complexity O(program size) — one closure walk plus the intern.
    /// @alloc closure scratch (the pc vector + per-pc visited flags), then whatever
    ///   @ref intern_state keeps for the state.
    /// @test RegexE2E.Anchors
    int start_state(int entry) {
        std::vector<int> out;
        std::vector<char> seen(prog.size(), 0);
        add_closure(entry, out, seen);
        return intern_state(std::move(out));
    }

    /// Transition from PACKED @p state on input byte @p b, lazily filling the cache.
    /// @param state the current packed state id (its row's byte offset). @param b the input
    /// byte. @return the next packed state id; the accept flag is read separately from the
    ///   destination row's @ref kFlagSlot, off the loop-carried chain.
    /// @complexity O(1) on a cached transition — one table load; a cache miss pays one
    ///   O(program size) closure walk and interns the successor (this is the "lazy" in
    ///   lazy-DFA — each (state, byte) pair is computed at most once, keeping match time
    ///   linear).
    /// @alloc none on a cache hit; a miss allocates closure scratch plus whatever
    ///   @ref intern_state keeps.
    /// @test RegexE2E.SearchPresentAbsent
    int step(int state, unsigned char b) {
        const std::size_t slot = static_cast<std::size_t>(state) / sizeof(int) + b;
        int cached = tflat[slot];
        if (cached != -1) return cached;
        std::vector<int> out;
        std::vector<char> seen(prog.size(), 0);
        for (int pc : pcs[static_cast<std::size_t>(state) / kRowBytes]) {
            const Inst& in = prog[pc];
            if (in.op == Inst::Byte && has_bit(in.cls, b)) add_closure(in.x, out, seen);
        }
        int next = pack(intern_state(std::move(out)));  // may grow tflat -> index AFTER, not before
        tflat[slot] = next;
        return next;
    }
};

Pattern compile(std::string_view pattern) {
    Pattern out;
    if (pattern.size() > kMaxPatternLength) {
        out.error = "pattern too long";
        return out;
    }
    auto dfa = std::make_shared<Dfa>();
    if (!pattern.empty() && pattern.front() == '^') { dfa->anchored_start = true; pattern.remove_prefix(1); }
    if (!pattern.empty() && pattern.back() == '$') { dfa->anchored_end = true; pattern.remove_suffix(1); }

    Compiler c;
    c.prog.reserve(pattern.size() + 4);  // ~one instruction per pattern byte + prefix + match
    // Slots 0..1 = the unanchored `.*?` prefix, so an unanchored search begins a match at ANY
    // position with no per-position work: 0: Split(body, 1); 1: Byte(any) -> 0.
    c.emit(Inst{});
    Inst anybyte; anybyte.op = Inst::Byte;
    for (unsigned long & cl : anybyte.cls) cl = ~std::uint64_t{0};
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
        int cnt = 0;
        for (unsigned long w : dfa->first) cnt += std::popcount(w);
        dfa->first_count = cnt;
        if (cnt == 1)  // locate the single set bit; the byte-indexed LUT is built lazily on use
            for (int w = 0; w < 4; ++w)
                if (dfa->first[w]) {
                    dfa->first_byte =
                        static_cast<unsigned char>(w * 64 + std::countr_zero(dfa->first[w]));
                    break;
                }
    }

    // The literal chain the pattern must start with: single-byte Byte instructions linked in a
    // straight line from the anchored entry. Two or more bytes arm the front+back candidate
    // probe (memchr the first byte, verify the last at its fixed distance) — far fewer false
    // candidates than a first-byte scan on text where that byte is common.
    for (int pc = dfa->start_anchored; dfa->lit.size() < 32;) {
        const Inst& in = dfa->prog[static_cast<std::size_t>(pc)];
        if (in.op != Inst::Byte) break;
        const int bits = std::popcount(in.cls[0]) + std::popcount(in.cls[1]) +
                         std::popcount(in.cls[2]) + std::popcount(in.cls[3]);
        if (bits != 1) break;  // empty or multi-byte class: the chain ends here
        for (int w = 0; w < 4; ++w)
            if (in.cls[w]) {
                dfa->lit += static_cast<char>(w * 64 + std::countr_zero(in.cls[w]));
                break;
            }
        pc = in.x;
    }
    if (dfa->lit.size() < 2) dfa->lit.clear();

    // `$` without `^`: every match must END at end-of-input, so matching runs BACKWARD over a
    // reversed program — one anchored pass from the end answers existence and yields the
    // leftmost begin, instead of forward-scanning every candidate start. Reversal only flips
    // concatenation order (`|`/`*`/`+`/`?` are direction-symmetric, atoms are single
    // instructions), and the source just parsed clean, so this second pass cannot fail.
    if (dfa->anchored_end && !dfa->anchored_start) {
        Compiler rc;
        rc.reversed = true;
        rc.src = pattern;
        Frag rf = rc.parse_alt();
        Inst rm; rm.op = Inst::Match;
        const int rmatch = rc.emit(rm);
        rc.patch(rf.holes, rmatch);
        auto rev = std::make_unique<Dfa>();
        rev->prog = std::move(rc.prog);
        rev->start_anchored = rf.start;
        dfa->rev = std::move(rev);
    }

    out.impl = std::move(dfa);
    out.ok = true;
    return out;
}

namespace {
// The per-byte transition, structured so the scan loops keep the transition table's data
// pointer `tf` IN A REGISTER: the cache-hit path is one load off `tf`, and only the rare
// miss calls into `Dfa::step` (which may grow the table) and refreshes `tf`. Reading the
// table through the member every byte would force a reload per byte — `step`'s store makes
// the compiler assume the vector moved.
inline int step_fast(Dfa& d, const int*& tf, int state, unsigned char b) {
    const int cached = *reinterpret_cast<const int*>(reinterpret_cast<const char*>(tf) +
                                                     static_cast<std::size_t>(state) +
                                                     (static_cast<std::size_t>(b) << 2));
    if (cached >= 0) return cached;
    const int next = d.step(state, b);
    tf = d.tflat.data();
    return next;
}

// The accept flag of packed state `s` — an independent load of its row's flag slot. It
// consumes the just-loaded state but never feeds the next transition's address, so it adds
// nothing to the loop-carried chain.
inline bool accepting(const int* tf, int state) {
    return tf[static_cast<std::size_t>(state) / sizeof(int) + Dfa::kFlagSlot] != 0;
}

// Same-byte run skipping. When a step observes `S --c--> S` (the state just mapped a byte
// back onto itself — the transition is cached, it was just taken) and S is non-accepting
// (the loops return on accept before reaching the skip), then every CONSECUTIVE following
// `c` provably leaves the machine in S with no accept missed — so the whole run can be
// jumped in one SWAR scan (8 bytes per compare) instead of stepped byte-by-byte. Long
// same-byte runs are a real input shape: padding, whitespace, repeated data.
std::size_t skip_run_forward(const char* base, std::size_t p, std::size_t n, char c) {
    std::uint64_t pat = 0;
    std::memset(&pat, static_cast<unsigned char>(c), sizeof pat);
    while (p + 8 <= n) {
        std::uint64_t w = 0;
        std::memcpy(&w, base + p, 8);
        if (w != pat) break;
        p += 8;
    }
    while (p < n && base[p] == c) ++p;
    return p;
}

// The backward twin: returns the smallest q <= p with bytes [q, p) all equal to `c`.
std::size_t skip_run_backward(const char* base, std::size_t p, char c) {
    std::uint64_t pat = 0;
    std::memset(&pat, static_cast<unsigned char>(c), sizeof pat);
    while (p >= 8) {
        std::uint64_t w = 0;
        std::memcpy(&w, base + p - 8, 8);
        if (w != pat) break;
        p -= 8;
    }
    while (p > 0 && base[p - 1] == c) --p;
    return p;
}

// The literal-chain compare, inlined: `k` is at most 32, so a plain byte loop beats a
// libc memcmp call (which cannot inline a runtime-length compare) at every probe site.
bool lit_eq(const char* p, const char* lp, std::size_t k) {
    for (std::size_t j = 0; j < k; ++j)
        if (p[j] != lp[j]) return false;
    return true;
}

// Advance `i` to the next position that could begin a match, using the strongest precomputed
// pruner the pattern allows: a required literal chain, a single required first byte (memchr),
// or the first-set LUT (expanded from the bitset on first use, so patterns that never take
// this branch never pay for it). Returns false when no candidate exists at or after `i`.
// Callers guarantee the pattern cannot match empty (an empty match needs no first byte) and
// that `first_count > 0`.
bool skip_to_candidate(Dfa& d, std::string_view text, std::size_t& i) {
    const char* base = text.data();
    const std::size_t n = text.size();
    if (!d.lit.empty()) {
        // The pattern must start with the literal `lit`, so candidates are exactly its
        // occurrences. Rare front byte: memchr sweeps at memory speed with few false hits.
        // Front-byte storm (8 false hits inside one 1 KiB window): switch to a 32-wide
        // branchless front+back block compare — the OR-reduction vectorizes, and the back
        // byte at its fixed distance filters almost everything before the byte-wise verify.
        // (The simultaneous two-byte probe is the idea behind RE2's prefix accelerator.)
        const std::size_t k = d.lit.size();
        const char front = d.lit[0];
        const char back = d.lit[k - 1];
        const char* lp = d.lit.data();
        int false_hits = 0;
        std::size_t window = i;
        while (i + k <= n) {
            const void* hit = std::memchr(base + i, front, n - k + 1 - i);
            if (!hit) return false;
            i = static_cast<std::size_t>(static_cast<const char*>(hit) - base);
            if (lit_eq(base + i, lp, k)) return true;
            ++i;
            if (++false_hits < 8) continue;
            if (i - window >= 1024) { false_hits = 0; window = i; continue; }  // sparse: stay
            break;                                                             // dense: block-scan
        }
        while (i + k + 31 <= n) {  // 32 candidate positions per iteration, branch-free
            bool any = false;
            for (unsigned j = 0; j < 32; ++j)
                any |= static_cast<int>(base[i + j] == front) & static_cast<int>(base[i + j + k - 1] == back);
            if (!any) { i += 32; continue; }
            const std::size_t block_end = i + 32;
            for (; i < block_end; ++i)
                if (base[i] == front && base[i + k - 1] == back && lit_eq(base + i, lp, k))
                    return true;
        }
        for (; i + k <= n; ++i)  // tail
            if (base[i] == front && base[i + k - 1] == back && lit_eq(base + i, lp, k))
                return true;
        return false;
    }
    if (d.first_count == 1) {
        const void* hit = std::memchr(base + i, d.first_byte, n - i);
        if (!hit) return false;
        i = static_cast<std::size_t>(static_cast<const char*>(hit) - base);
        return true;
    }
    if (!d.in_first_ready) {
        for (int b = 0; b < 256; ++b)
            d.in_first[static_cast<std::size_t>(b)] =
                has_bit(d.first, static_cast<unsigned char>(b)) ? 1 : 0;
        d.in_first_ready = true;
    }
    while (i < n && !d.in_first[static_cast<unsigned char>(base[i])]) ++i;
    return i < n;
}

// Run the anchored DFA from byte offset `from`; true if it reaches an accepting state (that
// also satisfies the end-anchor, if any). Touches only `text`'s bytes. `start` is PACKED.
bool run_anchored(Dfa& d, std::string_view text, std::size_t from, bool need_end, int start) {
    int state = start;
    const int* tf = d.tflat.data();
    for (std::size_t p = from;; ++p) {
        if (accepting(tf, state) && (!need_end || p == text.size())) return true;
        if (p == text.size()) return false;
        state = step_fast(d, tf, state, static_cast<unsigned char>(text[p]));
        if (state == 0) return false;  // dead — cannot extend
    }
}

// One forward pass from the unanchored start: the built-in `.*?` prefix keeps every possible
// start position alive inside the DFA state itself, so an absent pattern costs exactly one
// visit per byte — no per-candidate restarts, O(n) always. While the scan sits IN the start
// state (no partial match alive, canonical interning makes the id compare exact), the
// candidate skip may jump it forward; any non-starting byte re-arms it. The inner loop is one
// packed-table load and one flag test per byte.
bool run_unanchored(Dfa& d, std::string_view text) {
    const int ustart = d.cached_start(d.start_unanchored, d.ustart_id);
    int state = ustart;
    const int* tf = d.tflat.data();
    const bool can_skip = d.first_count > 0;
    const char* base = text.data();
    const std::size_t n = text.size();
    for (std::size_t p = 0; p < n;) {
        if (state == ustart) {
            // Every byte the start state maps back onto itself is free to skip — including
            // first-set bytes that only feed a self-loop (learned below). The memchr/literal
            // pruners then jump over the rest.
            while (p < n && d.ustart_self[static_cast<unsigned char>(base[p])]) ++p;
            if (p == n) return false;
            if (can_skip && !skip_to_candidate(d, text, p)) return false;
        }
        for (;;) {  // always steps at least once (p < n here), then runs until the text ends or the start state recurs
            const int prev = state;
            const auto b = static_cast<unsigned char>(base[p]);
            state = step_fast(d, tf, state, b);
            ++p;
            if (accepting(tf, state)) return true;
            if (state == prev) {
                if (state == ustart) d.ustart_self[b] = 1;  // learn the start self-loop
                else p = skip_run_forward(base, p, n, static_cast<char>(b));  // S--b-->S run
            }
            if (p >= n || state == ustart) break;
        }
    }
    return false;
}

// `$` without `^`: a match must end at end-of-input, so one anchored pass of the REVERSED
// program, walking backward from the end, answers existence — and dies after a handful of
// bytes when the tail can't match (`1274$` over a log that doesn't end in "1274").
bool run_reverse(Dfa& rd, std::string_view text) {
    int state = rd.cached_start(rd.start_anchored, rd.astart_id);
    const int* tf = rd.tflat.data();
    const char* base = text.data();
    std::size_t p = text.size();
    while (p > 0) {
        const int prev = state;
        const auto b = static_cast<unsigned char>(base[--p]);
        state = step_fast(rd, tf, state, b);
        if (accepting(tf, state)) return true;
        if (state == 0) return false;  // dead — no longer suffix can match either
        if (state == prev) p = skip_run_backward(base, p, static_cast<char>(b));  // S--b-->S run
    }
    return false;
}

bool run(const Pattern& re, std::string_view text, bool whole) {
    if (!re.ok || !re.impl) return false;
    Dfa& d = *re.impl;
    const bool need_end = whole || d.anchored_end;
    if (whole || d.anchored_start)  // pinned to the start
        return run_anchored(d, text, 0, need_end, d.cached_start(d.start_anchored, d.astart_id));
    // A pattern that can match the empty string ALWAYS matches an unanchored search: at position 0
    // when there is no end anchor, and at end-of-input when there is one (`a*$` over "bbb" — an
    // empty match at n satisfies `$`). This must short-circuit BEFORE the accelerated scans below,
    // which only propose positions holding a possible first byte (an empty match needs none).
    if (d.matches_empty) return true;
    if (d.rev) return run_reverse(*d.rev, text);  // `$` only: one backward anchored pass
    return run_unanchored(d, text);
}
}  // namespace

bool search(const Pattern& re, std::string_view text) { return run(re, text, false); }
bool full_match(const Pattern& re, std::string_view text) { return run(re, text, true); }

Match find(const Pattern& re, std::string_view text) {
    Match r;
    if (!re.ok || !re.impl) return r;
    Dfa& d = *re.impl;
    const std::size_t n = text.size();

    // `$` without `^`: the match end is pinned at n, so the leftmost-longest match is simply
    // the SMALLEST position whose suffix matches — one backward pass of the reversed program,
    // tracking the leftmost accept until the reverse DFA dies. A nullable pattern (`a*$`)
    // seeds the empty match at n, then extends leftward.
    if (d.rev) {
        Dfa& rd = *d.rev;
        int state = rd.cached_start(rd.start_anchored, rd.astart_id);
        const int* tf = rd.tflat.data();
        const char* base = text.data();
        long best = d.matches_empty ? static_cast<long>(n) : -1;
        std::size_t p = n;
        while (p > 0) {
            const int prev = state;
            const auto b = static_cast<unsigned char>(base[--p]);
            state = step_fast(rd, tf, state, b);
            if (accepting(tf, state)) best = static_cast<long>(p);
            if (state == 0) break;  // dead — no earlier begin can reach the end
            if (state == prev) {
                // A self-looping state keeps its accept flag across the whole run: skipping
                // it wholesale keeps `best` exact (every skipped begin was the same verdict,
                // and leftmost wins — the run's far end).
                p = skip_run_backward(base, p, static_cast<char>(b));
                if (accepting(tf, state)) best = static_cast<long>(p);
            }
        }
        if (best < 0) return r;
        r.matched = true;
        r.begin = static_cast<std::size_t>(best);
        r.end = n;
        r.text.assign(text.data() + r.begin, n - r.begin);  // OWN a copy of the matched bytes
        return r;
    }

    const std::size_t last_start = d.anchored_start ? 0 : n;
    const bool can_skip = !d.anchored_start && !d.matches_empty && d.first_count > 0;
    const int astart = d.cached_start(d.start_anchored, d.astart_id);
    // Candidate-dense absent input would try every position; after a budget of failed
    // candidates, ONE unanchored pass settles existence (absent input then costs O(n) total).
    // A present match just keeps the candidate loop going — leftmost-longest needs it anyway.
    long budget = 4096;
    const int* tf = d.tflat.data();
    for (std::size_t i = 0; i <= last_start; ++i) {
        if (can_skip && !skip_to_candidate(d, text, i)) return r;
        if (--budget == 0) {
            if (!run_unanchored(d, text)) return r;
            tf = d.tflat.data();  // the existence pass may have grown the table
        }
        int state = astart;
        long best_end = -1;
        for (std::size_t p = i;; ++p) {
            if (accepting(tf, state) && (!d.anchored_end || p == n)) best_end = static_cast<long>(p);
            if (p == n) break;
            state = step_fast(d, tf, state, static_cast<unsigned char>(text[p]));
            if (state == 0) break;  // dead — no match can extend
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
