// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// rxbench — 4-engine regex benchmark: cheatah::regex vs std::regex vs boost::regex vs Google RE2,
// on many patterns, input sizes, input shapes, and adversarial (catastrophic-backtracking) inputs.
//
// Every case is a BM_<section>_<row>_<engine> group over IDENTICAL inputs. Before anything is
// timed, every engine's answers are cross-checked on the exact benchmark corpora and the binary
// abort()s on disagreement — a benchmark that times the wrong answer is worthless. After the runs,
// main() prints a per-rival win/parity/loss tally (1.15x ratio threshold + 0.25 ns absolute floor,
// the bench_gate.sh convention) and an honest-losses list. RXBENCH_ASSERT=1 makes the process exit
// non-zero if cheatah loses any case to RE2.
//
// RE2 is timed in its OUT-OF-BOX configuration (UTF-8, leftmost-first) — what real RE2 users get.
// Offset/count parity is checked against RE2 with longest_match + Latin-1 (cheatah's documented
// leftmost-longest byte semantics); that configuration is never timed. std::regex and boost::regex
// are leftmost-first, so they participate in boolean parity only. The 4 MB pattern-table corpus is
// trimmed of its trailing newline so `$` means the same thing in all four engines (Perl-style `$`
// would otherwise also match before a final newline in some of them).
//
//     cmake -S stdlib/regex/bench -B build/regexbench -DCMAKE_BUILD_TYPE=Release
//     cmake --build build/regexbench -j
//     ./build/regexbench/rxbench --benchmark_repetitions=7 --benchmark_report_aggregates_only=true

#include "engines.hpp"

#include <benchmark/benchmark.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

using HayPtr = std::shared_ptr<const std::string>;

std::string make_log(std::size_t bytes) {
    std::string s;
    const char* line = "2026-07-02 12:00:01 INFO  request id=48213 user=bob@example.com status=200 bytes=1274\n";
    s.reserve(bytes + 100);
    while (s.size() < bytes) s += line;
    return s;
}

// A mixed "realistic" corpus: JSON-ish records, request lines, key lists, hex tokens,
// timestamps and UUID-ish ids — the shapes the BM_real_* extraction rows hunt through.
std::string make_real(std::size_t bytes) {
    const char* recs[] = {
        "{\"user_id\": 48213, \"name\": \"bob\", \"ts\": \"12:00:01\", \"tok\": \"0x1f2e3d4c\"}\n",
        "GET /api/v2/items?id=8ac4-99b2-11ee HTTP/1.1 status=200 t=0.043\n",
        "field_one=alpha;field_two=beta9;field_three=gamma12; -- padding --------\n",
        "ERROR at 03:14:15 code=0xdeadbeef uuid=0123-4567-89ab retry=3\n",
    };
    std::string s;
    s.reserve(bytes + 100);
    for (std::size_t i = 0; s.size() < bytes; ++i) s += recs[i % 4];
    return s;
}

// Auto-scale a nanosecond value into a "value unit" cell (ns / us / ms / s).
std::string fmt_ns(double ns) {
    char buf[32];
    if (ns < 1e3)      snprintf(buf, sizeof buf, "%9.1f ns", ns);
    else if (ns < 1e6) snprintf(buf, sizeof buf, "%9.2f us", ns / 1e3);
    else if (ns < 1e9) snprintf(buf, sizeof buf, "%9.3f ms", ns / 1e6);
    else               snprintf(buf, sizeof buf, "%9.3f s ", ns / 1e9);
    return buf;
}

// ---- row tables ----------------------------------------------------------------------

struct Row { const char* tag; const char* pat; };

constexpr Row kTable[] = {
    {"literal_present", "status=200"},
    {"literal_absent",  "status=500"},
    {"prefix_class",    "id=[0-9]+"},
    {"digits",          "[0-9]+"},
    {"word",            "[a-zA-Z]+"},
    {"email",           "[a-z]+@[a-z.]+"},
    {"email_absent",    "[a-z]+@nowhere"},
    {"alternation",     "INFO|WARN|ERROR"},
    {"alt_absent",      "FATAL|PANIC|SEGV"},
    {"key_value",       "user=[a-z0-9.@]+"},
    {"ip_absent",       "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+"},
    {"dotstar",         "INFO.*status"},
    {"anchor_start",    "^2026"},
    {"anchor_end",      "1274$"},
    {"nested_groups",   "(id|user)=([a-z0-9]+)"},
    {"class_quant",     "[A-Z][a-z]*"},
    {"escapes",         "\\d+ \\w+"},
    {"repetition",      "([a-z]+=[^ ]+ ?)+"},
};

constexpr Row kCompile[] = {
    {"literal",     "status=200"},
    {"class_email", "[a-z]+@[a-z.]+"},
    {"alternation", "INFO|WARN|ERROR"},
    {"ip",          "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+"},
    {"repetition",  "([a-z]+=[^ ]+ ?)+"},
};

constexpr Row kFind[] = {
    {"digits",     "[0-9]+"},                 // hit at offset 0
    {"email",      "[a-z]+@[a-z.]+"},         // hit early in the first line
    {"anchor_end", "1274$"},                  // hit only at the very end of the corpus
    {"ip_absent",  "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+"},  // no hit: full scan
};

// Rows where leftmost-first and leftmost-longest yield the same matches, so all four engines
// (and both RE2 configurations) must report the same count.
constexpr Row kFindall[] = {
    {"digits",      "[0-9]+"},
    {"word",        "[a-zA-Z]+"},
    {"alternation", "INFO|WARN|ERROR"},
    {"key_value",   "user=[a-z0-9.@]+"},
};

struct FullRow { const char* tag; const char* pat; const char* text; };
constexpr FullRow kFull[] = {
    {"digits_yes", "[0-9]+",          "48213"},
    {"email_yes",  "[a-z]+@[a-z.]+",  "bob@example.com"},
    {"email_no",   "[a-z]+@[a-z.]+",  "bob@Example.com"},
};

constexpr int kRedosN[] = {16, 20, 24, 28};
constexpr const char* kRedosNested = "(a+)+$";   // classic nested-quantifier blowup
constexpr const char* kRedosAltstar = "(a|a)*c"; // alternation-star blowup

// Adversarially LARGE inputs: the same hostile shapes at tens of megabytes, where a linear
// engine must stay boring and a backtracker melts. Rows with `backtrackers == false` are the
// exponential ones — std::regex could not finish a single iteration at this size (boost's
// complexity guard would throw), so only the linear engines run them.
struct XlRow { const char* tag; const char* pat; const char* corpus; bool backtrackers; };
constexpr XlRow kXl[] = {
    {"redos_nested_16M",  "(a+)+$",           "as_bang", false},
    {"redos_altstar_16M", "(a|a)*c",          "as",      false},
    {"reverse_alive_16M", "c[ab]*$",          "as",      true},   // reverse DFA alive end-to-end
    {"email_absent_16M",  "[a-z]+@nowhere",   "log16",   true},   // dense-candidate full scan
    {"literal_storm_16M", "status=500",       "log16",   true},   // front-byte storm at scale
};
constexpr const char* kXlFindPat = "x[0-9]";  // find over 16M of 'x': the candidate-budget path

enum class Op { Search, SearchBytes, Full, Find, Count };

// The generic extra families: realistic extraction, same-byte-run shapes, tiny-input
// latency, late finds, and additional adversarial compositions. `backtrackers == false`
// excludes std/boost where their cost is super-linear at this size; `guarded` wraps the
// body in try/catch (boost's complexity guard throws at match time).
struct GRow {
    const char* section;
    const char* tag;
    const char* pat;
    const char* corpus;
    Op op;
    bool backtrackers;
    bool guarded;
};
constexpr GRow kExtra[] = {
    // realistic extraction over the 4 MB mixed corpus
    {"real", "quoted", "\"[^\"]*\"", "real4", Op::Search, true, false},
    {"real", "hex", "0x[0-9a-f]+", "real4", Op::Search, true, false},
    {"real", "hex_absent", "0X[0-9A-F]+", "real4", Op::Search, true, false},
    {"real", "timestamp", "[0-9]+:[0-9]+:[0-9]+", "real4", Op::Search, true, false},
    {"real", "uuid", "[0-9a-f]+-[0-9a-f]+-[0-9a-f]+", "real4", Op::Search, true, false},
    {"real", "keylist", "([a-z_]+=[a-z0-9]+;)+", "real4", Op::Search, true, false},
    {"real", "find_hex", "0x[0-9a-f]+", "real4", Op::Find, true, false},
    {"real", "findall_ts", "[0-9]+:[0-9]+:[0-9]+", "real256k", Op::Count, true, false},
    // same-byte-run shapes (the self-loop run-skip family)
    {"run", "class_absent_16M", "[ab]+c", "as", Op::Search, false, true},   // O(n^2) backtrack
    // A 16 MB MATCH overflows libstdc++ std::regex's recursive matcher (SIGSEGV — a crash,
    // not a slow answer), so the two long-match rows run linear engines only.
    {"run", "class_present_16M", "[ab]+c", "as_c", Op::Search, false, true},
    {"run", "spaces_16M", "\\S+", "spaces", Op::Search, true, false},
    {"run", "padded_literal_16M", "NEEDLE_[0-9]+", "padded", Op::Search, true, false},
    {"run", "tailclass_16M", "[^x]*x", "xs_tail", Op::Search, false, true},
    // tiny-input per-call latency
    {"tiny", "digit_hit", "[0-9]+", "t_7", Op::Search, true, false},
    {"tiny", "digit_miss", "[0-9]+", "t_x", Op::Search, true, false},
    {"tiny", "email_hit", "[a-z]+@[a-z.]+", "t_ab", Op::Search, true, false},
    {"tiny", "email_miss", "[a-z]+@[a-z.]+", "t_dash", Op::Search, true, false},
    {"tiny", "full_tok16", "id=[0-9]+ t=[0-9a-f]+", "t_tok", Op::Full, true, false},
    // find with the only match at the 99% position of 4 MB
    {"findlate", "xmarker_4M", "XMARKER[0-9]+", "late", Op::Find, true, false},
    // extra adversarial compositions
    {"redos2", "alt2_N16", "(a|aa)+$", "a16_bang", Op::Search, true, true},
    {"redos2", "alt2_N28", "(a|aa)+$", "a28_bang", Op::Search, true, true},
    {"redos2", "dotstar3_4M", ".*.*.*Q", "log4", Op::Search, false, true},  // O(n^3) backtrack
};

constexpr std::size_t kSweepSizes[] = {1u << 10, 1u << 14, 1u << 18, 1u << 20, 1u << 24};
constexpr const char* kSweepTags[] = {"1K", "16K", "256K", "1M", "16M"};
constexpr const char* kSweepPat = "CRITICAL[0-9]+";     // absent at every size: a full scan
constexpr const char* kHugePat = "NOSUCHTOKEN_[0-9]+";  // absent in 64 MB
constexpr const char* kShapePat = "user=[a-z]+";

// ---- corpora -------------------------------------------------------------------------

struct Corpora {
    HayPtr table;                 // 4 MB log, trailing '\n' trimmed (uniform `$` semantics)
    HayPtr slice;                 // 256 KB of the same log (find-all rows)
    HayPtr huge;                  // 64 MB
    std::vector<std::pair<const char*, HayPtr>> sweep;   // tag -> haystack
    std::vector<std::pair<const char*, HayPtr>> shapes;  // tag -> haystack
    std::vector<std::pair<std::string, HayPtr>> redos;   // tag ("nested_N16") -> input, + pattern per family
    std::vector<std::pair<std::string, std::string>> redos_pat;  // tag -> pattern
    std::map<std::string, HayPtr> xl;                    // adversarially-large corpora by key
    std::vector<std::pair<std::string, std::string>> compilescale;  // tag -> generated pattern
};

Corpora build_corpora() {
    Corpora c;
    {
        std::string t = make_log(4'000'000);
        if (!t.empty() && t.back() == '\n') t.pop_back();
        c.table = std::make_shared<const std::string>(std::move(t));
    }
    c.slice = std::make_shared<const std::string>(c.table->substr(0, 256 * 1024));
    c.huge = std::make_shared<const std::string>(make_log(64'000'000));
    for (std::size_t i = 0; i < std::size(kSweepSizes); ++i)
        c.sweep.emplace_back(kSweepTags[i], std::make_shared<const std::string>(make_log(kSweepSizes[i])));
    {
        // The raw log matches "user=[a-z]+" in every line, so a meaningful position sweep needs a
        // neutral base with every 'u' knocked out; "everywhere" keeps the raw log.
        std::string neutral = make_log(4'000'000);
        for (char& ch : neutral) if (ch == 'u') ch = 'x';
        c.shapes.emplace_back("start", std::make_shared<const std::string>("user=zzz " + neutral));
        c.shapes.emplace_back("end", std::make_shared<const std::string>(neutral + " user=zzz"));
        c.shapes.emplace_back("absent", std::make_shared<const std::string>(neutral));
        c.shapes.emplace_back("everywhere", std::make_shared<const std::string>(make_log(4'000'000)));
    }
    for (int n : kRedosN) {
        c.redos.emplace_back("nested_N" + std::to_string(n),
                             std::make_shared<const std::string>(std::string(std::size_t(n), 'a') + '!'));
        c.redos_pat.emplace_back("nested_N" + std::to_string(n), kRedosNested);
        c.redos.emplace_back("altstar_N" + std::to_string(n),
                             std::make_shared<const std::string>(std::string(std::size_t(n), 'a')));
        c.redos_pat.emplace_back("altstar_N" + std::to_string(n), kRedosAltstar);
    }
    constexpr std::size_t kXlBytes = 16u << 20;
    c.xl.emplace("as", std::make_shared<const std::string>(std::string(kXlBytes, 'a')));
    c.xl.emplace("as_bang", std::make_shared<const std::string>(std::string(kXlBytes, 'a') + '!'));
    c.xl.emplace("xs", std::make_shared<const std::string>(std::string(kXlBytes, 'x')));
    c.xl.emplace("log16", std::make_shared<const std::string>(make_log(kXlBytes)));
    c.xl.emplace("as_c", std::make_shared<const std::string>(std::string(kXlBytes, 'a') + 'c'));
    c.xl.emplace("xs_tail", std::make_shared<const std::string>(std::string(kXlBytes, 'a') + 'x'));
    c.xl.emplace("spaces",
                 std::make_shared<const std::string>(std::string(kXlBytes - 6, ' ') + "token6"));
    {
        std::string padded(kXlBytes, '-');
        padded.replace(15u << 20, 9, "NEEDLE_42");
        c.xl.emplace("padded", std::make_shared<const std::string>(std::move(padded)));
    }
    {
        std::string real = make_real(4'000'000);
        c.xl.emplace("real256k", std::make_shared<const std::string>(real.substr(0, 256 * 1024)));
        c.xl.emplace("real4", std::make_shared<const std::string>(std::move(real)));
    }
    {
        std::string late = make_log(4'000'000);
        if (!late.empty() && late.back() == '\n') late.pop_back();
        late.replace(late.size() * 99 / 100, 9, "XMARKER77");
        c.xl.emplace("late", std::make_shared<const std::string>(std::move(late)));
    }
    c.xl.emplace("log4", c.table);
    c.xl.emplace("t_7", std::make_shared<const std::string>("7"));
    c.xl.emplace("t_x", std::make_shared<const std::string>("x"));
    c.xl.emplace("t_ab", std::make_shared<const std::string>("a@b"));
    c.xl.emplace("t_dash", std::make_shared<const std::string>("a-b"));
    c.xl.emplace("t_tok", std::make_shared<const std::string>("id=48213 t=9f2e"));
    c.xl.emplace("a16_bang", std::make_shared<const std::string>(std::string(16, 'a') + '!'));
    c.xl.emplace("a28_bang", std::make_shared<const std::string>(std::string(28, 'a') + '!'));
    {
        std::string alt;
        for (int i = 0; i < 50; ++i) {
            if (i) alt += '|';
            alt += "alpha" + std::to_string(i);
        }
        c.compilescale.emplace_back("alt50", alt);
        std::string lit64;
        while (lit64.size() < 64) lit64 += "abcdefghijklmnop";
        c.compilescale.emplace_back("literal64", lit64);
        c.compilescale.emplace_back("nest100",
                                    std::string(100, '(') + "a" + std::string(100, ')'));
    }
    return c;
}

// ---- parity verification (abort on mismatch — never time a wrong answer) -------------

void die(const std::string& msg) {
    std::fprintf(stderr, "rxbench: PARITY FAILURE: %s\n", msg.c_str());
    std::abort();
}

// Cross-check one boolean operation for one pattern over one haystack, across every engine
// that accepts the pattern. `want` comes from cheatah (the engine under test). Backtracking
// engines may throw at match time (boost's complexity guard) — a throw skips that engine.
struct BoolParity {
    std::string pat;
    std::string_view hay;
    const char* label;
    bool with_std = true;
    bool with_boost = true;
};

template <eng::Engine E>
void check_bool_one(const BoolParity& p, bool full, bool want) {
    auto re = eng::try_compile<E>(p.pat);
    if (!re) return;  // engine rejects the pattern: nothing to compare
    try {
        const bool got = full ? E::full(*re, p.hay) : E::search(*re, p.hay);
        if (got != want)
            die(std::string(E::name) + (full ? " full_match" : " search") + " disagrees on '" + p.pat +
                "' over " + p.label + " (cheatah=" + (want ? "true" : "false") + ")");
    } catch (const std::exception&) {
        // match-time throw (backtracker complexity guard): no answer to compare
    }
}

void check_bool(const BoolParity& p, bool full = false) {
    auto ch = eng::try_compile<eng::Cheatah>(p.pat);
    if (!ch) die("cheatah cannot compile its own benchmark pattern '" + p.pat + "'");
    const bool want = full ? eng::Cheatah::full(*ch, p.hay) : eng::Cheatah::search(*ch, p.hay);
    if (p.with_std) check_bool_one<eng::Std>(p, full, want);
    if (p.with_boost) check_bool_one<eng::Boost>(p, full, want);
    check_bool_one<eng::Re2Def>(p, full, want);
    check_bool_one<eng::Re2Longest>(p, full, want);
}

// find offsets must agree with the leftmost-longest oracle (RE2 longest_match + Latin-1).
void check_find(const std::string& pat, std::string_view hay, const char* label) {
    auto ch = eng::try_compile<eng::Cheatah>(pat);
    auto oracle = eng::try_compile<eng::Re2Longest>(pat);
    if (!ch || !oracle) die("find parity: '" + pat + "' must compile in cheatah and RE2");
    std::size_t cb = 0, ce = 0, ob = 0, oe = 0;
    const bool cf = eng::Cheatah::find(*ch, hay, cb, ce);
    const bool of = eng::Re2Longest::find(*oracle, hay, ob, oe);
    if (cf != of || (cf && (cb != ob || ce != oe)))
        die("find disagrees with the RE2-longest oracle on '" + pat + "' over " + label +
            " (cheatah " + (cf ? std::to_string(cb) + ".." + std::to_string(ce) : "none") +
            ", oracle " + (of ? std::to_string(ob) + ".." + std::to_string(oe) : "none") + ")");
}

// Non-overlapping match counts must agree across all engines on unambiguous rows.
void check_count(const std::string& pat, std::string_view hay, const char* label) {
    auto ch = eng::try_compile<eng::Cheatah>(pat);
    if (!ch) die("count parity: cheatah rejects '" + pat + "'");
    const std::size_t want = eng::Cheatah::count_all(*ch, hay);
    auto one = [&]<eng::Engine E>() {
        auto re = eng::try_compile<E>(pat);
        if (!re) return;
        const std::size_t got = E::count_all(*re, hay);
        if (got != want)
            die(std::string(E::name) + " count_all disagrees on '" + pat + "' over " + label + " (" +
                std::to_string(got) + " vs cheatah " + std::to_string(want) + ")");
    };
    one.template operator()<eng::Std>();
    one.template operator()<eng::Boost>();
    one.template operator()<eng::Re2Def>();
    one.template operator()<eng::Re2Longest>();
}

void verify_parity(const Corpora& c) {
    std::fprintf(stderr, "rxbench: verifying engine agreement on the benchmark corpora...\n");
    for (const Row& r : kTable) check_bool({r.pat, *c.table, "the 4 MB log"});
    for (const auto& [tag, hay] : c.sweep) check_bool({kSweepPat, *hay, tag});
    for (const auto& [tag, hay] : c.shapes) check_bool({kShapePat, *hay, tag});
    check_bool({kHugePat, *c.huge, "the 64 MB log"});
    for (std::size_t i = 0; i < c.redos.size(); ++i) {
        // std::regex is exponential here: only the smallest N is affordable to cross-check.
        const bool small = c.redos[i].first.ends_with("N16");
        check_bool({c.redos_pat[i].second, *c.redos[i].second, c.redos[i].first.c_str(),
                    /*with_std=*/small, /*with_boost=*/true});
    }
    for (const FullRow& r : kFull) check_bool({r.pat, r.text, r.tag}, /*full=*/true);
    for (const Row& r : kFind) check_find(r.pat, *c.table, "the 4 MB log");
    for (const Row& r : kFindall) check_count(r.pat, *c.slice, "the 256 KB slice");
    for (const XlRow& r : kXl)  // backtrackers verify only where they can finish a single run
        check_bool({r.pat, *c.xl.at(r.corpus), r.tag, r.backtrackers, r.backtrackers});
    check_find(kXlFindPat, *c.xl.at("xs"), "16M of 'x'");
    for (const GRow& r : kExtra) {
        const bool bt = r.backtrackers && !r.guarded;  // guarded = too slow to verify, too
        if (r.op == Op::Find) check_find(r.pat, *c.xl.at(r.corpus), r.tag);
        else if (r.op == Op::Count) check_count(r.pat, *c.xl.at(r.corpus), r.tag);
        else check_bool({r.pat, *c.xl.at(r.corpus), r.tag, bt, bt}, r.op == Op::Full);
    }
    std::fprintf(stderr, "rxbench: all engines agree on every benchmarked case.\n");
}

// ---- benchmark bodies ----------------------------------------------------------------

template <eng::Engine E>
void run_search(benchmark::State& st, const typename E::Re& re, std::string_view h, bool bytes) {
    for (auto _ : st) {
        benchmark::DoNotOptimize(h);
        bool r = E::search(re, h);
        benchmark::DoNotOptimize(&r);
    }
    if (bytes) st.SetBytesProcessed(static_cast<int64_t>(st.iterations()) * static_cast<int64_t>(h.size()));
}

// Adversarial rows: a backtracking engine may throw mid-run (boost's complexity guard).
template <eng::Engine E>
void run_search_guarded(benchmark::State& st, const typename E::Re& re, std::string_view h) {
    for (auto _ : st) {
        benchmark::DoNotOptimize(h);
        try {
            bool r = E::search(re, h);
            benchmark::DoNotOptimize(&r);
        } catch (const std::exception& ex) {
            st.SkipWithMessage(std::string("threw: ") + ex.what());
            break;
        }
    }
}

template <eng::Engine E>
void run_full(benchmark::State& st, const typename E::Re& re, std::string_view h) {
    for (auto _ : st) {
        benchmark::DoNotOptimize(h);
        bool r = E::full(re, h);
        benchmark::DoNotOptimize(&r);
    }
}

template <eng::Engine E>
void run_find(benchmark::State& st, const typename E::Re& re, std::string_view h) {
    for (auto _ : st) {
        benchmark::DoNotOptimize(h);
        std::size_t b = 0, e = 0;
        bool r = E::find(re, h, b, e);
        benchmark::DoNotOptimize(&r);
        benchmark::DoNotOptimize(&b);
        benchmark::DoNotOptimize(&e);
    }
}

template <eng::Engine E>
void run_count(benchmark::State& st, const typename E::Re& re, std::string_view h) {
    for (auto _ : st) {
        benchmark::DoNotOptimize(h);
        std::size_t n = E::count_all(re, h);
        benchmark::DoNotOptimize(&n);
    }
}

template <eng::Engine E>
void run_compile(benchmark::State& st, const std::string& pat) {
    std::string p = pat;  // mutable copy: DoNotOptimize on a const ref is deprecated (and weaker)
    for (auto _ : st) {
        benchmark::DoNotOptimize(p);
        auto re = E::compile(p);
        benchmark::DoNotOptimize(&re);
    }
}

// ---- registration --------------------------------------------------------------------

void for_each_engine(auto&& f) {
    f.template operator()<eng::Cheatah>();
    f.template operator()<eng::Std>();
    f.template operator()<eng::Boost>();
    f.template operator()<eng::Re2Def>();
}

template <eng::Engine E>
void reg_one(const std::string& name, const char* pat, HayPtr hay, Op op) {
    auto re = eng::try_compile<E>(pat);
    if (!re) {
        std::fprintf(stderr, "rxbench: %s rejects '%s' — row skipped for this engine\n", E::name, pat);
        return;
    }
    benchmark::RegisterBenchmark(name.c_str(), [re, hay, op](benchmark::State& st) {
        std::string_view h(*hay);
        switch (op) {
            case Op::Search:      run_search<E>(st, *re, h, false); break;
            case Op::SearchBytes: run_search<E>(st, *re, h, true); break;
            case Op::Full:        run_full<E>(st, *re, h); break;
            case Op::Find:        run_find<E>(st, *re, h); break;
            case Op::Count:       run_count<E>(st, *re, h); break;
        }
    });
}

void register_benchmarks(const Corpora& c) {
    for_each_engine([&]<eng::Engine E>() {
        for (const Row& r : kCompile) {
            if (!eng::try_compile<E>(r.pat)) continue;
            const std::string pat = r.pat;
            benchmark::RegisterBenchmark((std::string("BM_compile_") + r.tag + "_" + E::name).c_str(),
                                         [pat](benchmark::State& st) { run_compile<E>(st, pat); });
        }
        for (const auto& [tag, pat] : c.compilescale) {
            if (!eng::try_compile<E>(pat)) continue;
            const std::string p = pat;
            benchmark::RegisterBenchmark((std::string("BM_compilescale_") + tag + "_" + E::name).c_str(),
                                         [p](benchmark::State& st) { run_compile<E>(st, p); });
        }
        for (const Row& r : kTable)
            reg_one<E>(std::string("BM_pat_") + r.tag + "_" + E::name, r.pat, c.table, Op::Search);
        for (const auto& [tag, hay] : c.sweep)
            reg_one<E>(std::string("BM_sweep_") + tag + "_" + E::name, kSweepPat, hay, Op::SearchBytes);
        for (const auto& [tag, hay] : c.shapes)
            reg_one<E>(std::string("BM_shape_") + tag + "_" + E::name, kShapePat, hay, Op::Search);
        reg_one<E>(std::string("BM_hugescan_") + E::name, kHugePat, c.huge, Op::SearchBytes);
        for (const Row& r : kFind)
            reg_one<E>(std::string("BM_find_") + r.tag + "_" + E::name, r.pat, c.table, Op::Find);
        for (const Row& r : kFindall)
            reg_one<E>(std::string("BM_findall_") + r.tag + "_" + E::name, r.pat, c.slice, Op::Count);
        for (const FullRow& r : kFull) {
            auto hay = std::make_shared<const std::string>(r.text);
            reg_one<E>(std::string("BM_full_") + r.tag + "_" + E::name, r.pat, hay, Op::Full);
        }
        for (std::size_t i = 0; i < c.redos.size(); ++i) {
            const std::string& tag = c.redos[i].first;
            const std::string& pat = c.redos_pat[i].second;
            HayPtr hay = c.redos[i].second;
            auto re = eng::try_compile<E>(pat);
            if (!re) continue;
            auto* b = benchmark::RegisterBenchmark(
                (std::string("BM_redos_") + tag + "_" + E::name).c_str(),
                [re, hay](benchmark::State& st) { run_search_guarded<E>(st, *re, std::string_view(*hay)); });
            // std::regex is exponential on these rows (~seconds at N=28): one shot, not min_time.
            if (std::string_view(E::name) == "std") b->Iterations(1);
        }
        const bool backtracker =
            std::string_view(E::name) == "std" || std::string_view(E::name) == "boost";
        for (const XlRow& r : kXl) {
            if (backtracker && !r.backtrackers) continue;  // exponential at 16 MB: unrunnable
            HayPtr hay = c.xl.at(r.corpus);
            auto re = eng::try_compile<E>(r.pat);
            if (!re) continue;
            auto* b = benchmark::RegisterBenchmark(
                (std::string("BM_xl_") + r.tag + "_" + E::name).c_str(),
                [re, hay](benchmark::State& st) { run_search_guarded<E>(st, *re, std::string_view(*hay)); });
            if (backtracker) b->Iterations(1);  // linear-but-slow engines: one shot is plenty
        }
        reg_one<E>(std::string("BM_xl_find_budget_16M_") + E::name, kXlFindPat, c.xl.at("xs"), Op::Find);
        for (const GRow& r : kExtra) {
            if (backtracker && !r.backtrackers) continue;  // super-linear at this size
            HayPtr hay = c.xl.at(r.corpus);
            const std::string name = std::string("BM_") + r.section + "_" + r.tag + "_" + E::name;
            if (r.guarded && r.op == Op::Search) {
                auto re = eng::try_compile<E>(r.pat);
                if (!re) continue;
                auto* b = benchmark::RegisterBenchmark(name.c_str(), [re, hay](benchmark::State& st) {
                    run_search_guarded<E>(st, *re, std::string_view(*hay));
                });
                if (backtracker) b->Iterations(1);  // exponential-or-slow one-shots
            } else {
                reg_one<E>(name, r.pat, hay, r.op);
            }
        }
    });
}

// ---- tally: per-rival win/parity/loss + honest losses --------------------------------

constexpr double kThreshold = 1.15;  // ratio floor before a difference counts (bench_gate.sh)
constexpr double kMinGapNs = 0.25;   // absolute floor: ~one cycle; below it a ratio is noise

class TallyReporter : public benchmark::ConsoleReporter {
  public:
    std::map<std::string, std::vector<double>> raw;   // case -> per-repetition real ns
    std::map<std::string, double> median_agg;         // case -> reported median real ns

    void ReportRuns(const std::vector<Run>& runs) override {
        for (const Run& r : runs) {
            if (r.skipped) continue;
            const std::string name = r.run_name.str();
            if (r.run_type == Run::RT_Aggregate) {
                if (r.aggregate_name == "median") median_agg[name] = r.GetAdjustedRealTime();
            } else {
                raw[name].push_back(r.GetAdjustedRealTime());
            }
        }
        ConsoleReporter::ReportRuns(runs);
    }
};

double median_of(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// One rival's comparison in a row: ratio (rival/cheatah, >1 = cheatah faster) + verdict.
std::string verdict_cell(double che, double rival) {
    const char* mark = "\xE2\x9A\xAA";  // ⚪ parity
    if (che > rival * kThreshold && (che - rival) > kMinGapNs) mark = "\xE2\x9D\x8C";  // ❌
    else if (rival > che * kThreshold && (rival - che) > kMinGapNs) mark = "\xE2\x9C\x85";  // ✅
    char buf[48];
    snprintf(buf, sizeof buf, "%.2fx %s", rival / che, mark);
    return buf;
}

// Exit status: 0, or 1 when RXBENCH_ASSERT=1 and cheatah lost at least one case to RE2.
// RXBENCH_TABLE=<path> additionally writes the complete comparison as a Markdown table.
int print_tally(const TallyReporter& rep) {
    std::map<std::string, double> ns;
    for (const auto& [name, v] : rep.raw) ns[name] = median_of(v);
    for (const auto& [name, v] : rep.median_agg) ns[name] = v;  // median aggregate wins over raw

    struct Rival { const char* suffix; const char* label; int win = 0, par = 0, loss = 0; };
    Rival rivals[] = {{"_std", "std"}, {"_boost", "boost"}, {"_re2", "re2"}};
    std::vector<std::string> loss_lines;
    std::string md;

    std::printf("\n==== per-case medians ====\n");
    std::printf("%-28s %12s %12s %12s %12s  %11s\n", "case", "cheatah", "std", "boost", "re2", "re2/cheatah");
    for (const auto& [name, che] : ns) {
        constexpr std::string_view kMe = "_cheatah";
        if (name.size() <= kMe.size() || name.substr(name.size() - kMe.size()) != kMe) continue;
        const std::string base = name.substr(0, name.size() - kMe.size());
        std::string cells[3], mdcells[3];
        double re2_ratio = 0.0;
        for (std::size_t i = 0; i < 3; ++i) {
            auto it = ns.find(base + rivals[i].suffix);
            if (it == ns.end()) { cells[i] = "-"; mdcells[i] = "—"; continue; }
            const double rival = it->second;
            cells[i] = fmt_ns(rival);
            mdcells[i] = verdict_cell(che, rival);
            if (i == 2) re2_ratio = rival / che;
            if (che > rival * kThreshold && (che - rival) > kMinGapNs) {
                ++rivals[i].loss;
                if (i == 2)
                    loss_lines.push_back("vs re2: " + base.substr(3) + " — cheatah " + fmt_ns(che) +
                                         " vs re2 " + fmt_ns(rival) + "  (" +
                                         std::to_string(che / rival).substr(0, 5) + "x slower)");
            } else if (rival > che * kThreshold && (rival - che) > kMinGapNs) {
                ++rivals[i].win;
            } else {
                ++rivals[i].par;
            }
        }
        std::printf("%-28s %12s %12s %12s %12s  %10.2fx\n", base.substr(3).c_str(), fmt_ns(che).c_str(),
                    cells[0].c_str(), cells[1].c_str(), cells[2].c_str(), re2_ratio);
        auto trim = [](std::string s) {
            while (!s.empty() && s.front() == ' ') s.erase(s.begin());
            return s;
        };
        std::string sc[3];
        for (std::size_t i = 0; i < 3; ++i) {
            auto it = ns.find(base + rivals[i].suffix);
            sc[i] = (it == ns.end()) ? std::string("—") : trim(fmt_ns(it->second));
        }
        md += "| " + base.substr(3) + " | " + trim(fmt_ns(che)) + " | " + sc[0] + " | " + sc[1] +
              " | " + sc[2] + " | " + mdcells[0] + " | " + mdcells[1] + " | " + mdcells[2] + " |\n";
    }

    std::printf("\n==== tally (faster: >%.2fx and >%.2f ns apart; else parity) ====\n", kThreshold, kMinGapNs);
    for (const Rival& r : rivals)
        std::printf("vs %-6s %3d faster, %3d parity, %3d slower\n", r.label, r.win, r.par, r.loss);

    if (loss_lines.empty()) {
        std::printf("\nhonest losses vs re2: none — cheatah ties or beats RE2 on every case.\n");
    } else {
        std::printf("\nhonest losses vs re2:\n");
        for (const std::string& l : loss_lines) std::printf("  %s\n", l.c_str());
    }

    if (const char* path = std::getenv("RXBENCH_TABLE")) {
        std::FILE* f = std::fopen(path, "w");
        if (f) {
            std::time_t now = std::time(nullptr);
            char day[16] = "unknown";
            if (std::tm* tm = std::localtime(&now)) std::strftime(day, sizeof day, "%Y-%m-%d", tm);
            std::fprintf(f,
                         "<!-- generated by rxbench (stdlib/regex/bench) on %s — medians; ratio = "
                         "rival/cheatah, >1 means cheatah is faster; verdicts use the %.2fx + %.2f ns "
                         "band -->\n\n"
                         "| case | cheatah | std::regex | boost | RE2 | vs std | vs boost | vs RE2 |\n"
                         "|---|--:|--:|--:|--:|--:|--:|--:|\n%s\n",
                         day, kThreshold, kMinGapNs, md.c_str());
            std::fprintf(f, "**Tally** — ");
            for (const Rival& r : rivals)
                std::fprintf(f, "vs %s: **%d faster / %d parity / %d slower**%s", r.label, r.win,
                             r.par, r.loss, (&r == &rivals[2]) ? ".\n" : "; ");
            if (loss_lines.empty())
                std::fprintf(f, "\ncheatah ties or beats RE2 on **every** case.\n");
            else {
                std::fprintf(f, "\nLosses vs RE2:\n");
                for (const std::string& l : loss_lines) std::fprintf(f, "- %s\n", l.c_str());
            }
            std::fclose(f);
            std::printf("\ncomparison table written to %s\n", path);
        }
    }

    const char* assert_env = std::getenv("RXBENCH_ASSERT");
    if (assert_env && assert_env[0] == '1' && !loss_lines.empty()) {
        std::printf("\nRXBENCH_ASSERT: FAILING — cheatah lost %zu case(s) to RE2.\n", loss_lines.size());
        return 1;
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Corpora corpora = build_corpora();
    verify_parity(corpora);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    register_benchmarks(corpora);
    TallyReporter reporter;
    benchmark::RunSpecifiedBenchmarks(&reporter);
    benchmark::Shutdown();
    return print_tally(reporter);
}
