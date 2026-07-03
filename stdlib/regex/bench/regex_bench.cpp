// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Benchmark: cheatah::regex vs std::regex vs boost::regex on many string sizes, input shapes,
// and adversarial (catastrophic-backtracking) inputs.
//
//   without Boost:  c++ -O3 -std=c++20 regex_bench.cpp ../regex.cpp -o rxbench
//   WITH    Boost:  c++ -O3 -std=c++20 -DCHEATAH_REGEX_BENCH_BOOST regex_bench.cpp ../regex.cpp \
//                       -o rxbench -lboost_regex
#include "../regex.hpp"
#include <chrono>
#include <cstdio>
#include <regex>
#include <string>
#include <string_view>
#include <vector>
#ifdef CHEATAH_REGEX_BENCH_BOOST
#include <boost/regex.hpp>
#define HAVE_BOOST 1
#else
#define HAVE_BOOST 0
#endif
namespace rx = cheatah::regex;
using clk = std::chrono::steady_clock;

template <class F> double best_ms(F f, int reps = 7) {
    double b = 1e18;
    for (int i = 0; i < reps; ++i) {
        auto t0 = clk::now(); f();
        double ms = std::chrono::duration<double, std::milli>(clk::now() - t0).count();
        if (ms < b) b = ms;
    }
    return b;
}
// Auto-scale a millisecond value into a right-aligned "value unit" cell (ns / us / ms).
static std::string fmt(double ms) {
    char buf[32];
    double ns = ms * 1e6;
    if (ns < 1000.0)        snprintf(buf, sizeof buf, "%7.1f ns", ns);
    else if (ns < 1e6)      snprintf(buf, sizeof buf, "%7.2f us", ns/1e3);
    else                    snprintf(buf, sizeof buf, "%7.3f ms", ms);
    return buf;
}
static std::string make_log(std::size_t bytes) {
    std::string s;
    const char* line = "2026-07-02 12:00:01 INFO  request id=48213 user=bob@example.com status=200 bytes=1274\n";
    s.reserve(bytes + 100);
    while (s.size() < bytes) s += line;
    return s;
}

int main() {
    printf("engines: cheatah + std::regex%s\n\n", HAVE_BOOST ? " + boost::regex" : " (Boost NOT compiled in — add -DCHEATAH_REGEX_BENCH_BOOST -lboost_regex)");

    // (0) PATTERN TABLE — many patterns on one 4 MB log; cheatah/std/boost throughput.
    {
        std::string hay = make_log(4'000'000); std::string_view H = hay; double mb = hay.size()/1e6;
        struct P { const char* name; const char* pat; };
        std::vector<P> pats = {
            {"literal (present)",   "status=200"},
            {"literal (absent)",    "status=500"},
            {"prefix+class",        "id=[0-9]+"},
            {"digits",              "[0-9]+"},
            {"word",                "[a-zA-Z]+"},
            {"email",               "[a-z]+@[a-z.]+"},
            {"email absent",        "[a-z]+@nowhere"},
            {"alternation",         "INFO|WARN|ERROR"},
            {"alt absent",          "FATAL|PANIC|SEGV"},
            {"key=value",           "user=[a-z0-9.@]+"},
            {"IP-ish",              "[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+"},
            {"dotstar",             "INFO.*status"},
            {"anchored ^",          "^2026"},
            {"anchored $",          "1274$"},
            {"nested groups",       "(id|user)=([a-z0-9]+)"},
            {"class + quant",       "[A-Z][a-z]*"},
            {"escape \\d\\w",       "\\d+ \\w+"},
            {"repetition-heavy",    "([a-z]+=[^ ]+ ?)+"},
        };
        printf("== pattern table: 4 MB log, best of 7 (ms) ==\n");
        printf("%-20s %11s %11s %11s  %8s %9s\n","pattern","cheatah","std","boost","vs std","vs boost");
        for (auto& pr : pats) {
            auto cre = rx::compile(pr.pat);
            if (!cre.ok) { printf("%-20s  COMPILE ERROR: %s\n", pr.name, cre.error.c_str()); continue; }
            std::regex sre; bool sbad=false; try { sre.assign(pr.pat, std::regex::optimize); } catch(...) { sbad=true; }
            volatile bool sink=false; (void)rx::search(cre,H);
            double ct=best_ms([&]{ sink=rx::search(cre,H); });
            double st=-1; if(!sbad) try { st=best_ms([&]{ sink=std::regex_search(hay,sre); }); } catch(...) {}
            double bt=-1;
#if HAVE_BOOST
            try { boost::regex bx(pr.pat); bt=best_ms([&]{ sink=boost::regex_search(hay,bx); }); } catch(...) {}
#endif
            (void)sink;(void)mb;
            printf("%-20s %11s", pr.name, fmt(ct).c_str());
            if(st<0) printf(" %11s","-"); else printf(" %11s", fmt(st).c_str());
            if(bt<0) printf(" %11s", HAVE_BOOST?"THREW":"-"); else printf(" %11s", fmt(bt).c_str());
            if(st>0) printf("  %7.1fx", st/ct); else printf("  %8s","-");
            if(bt>0) printf(" %8.1fx", bt/ct); else printf(" %9s", HAVE_BOOST?"(threw)":"-");
            printf("\n");
        }
        printf("\n");
    }

    // (1) SIZE SWEEP — absent pattern -> full scan. Linear DFA => constant MB/s.
    printf("== size sweep: absent \"CRITICAL[0-9]+\" (full scan), best of 7 ==\n");
    printf("%9s %9s %9s%s   ch MB/s  ch/std%s\n", "size", "cheatah", "std", HAVE_BOOST?"     boost":"", HAVE_BOOST?"  ch/boost":"");
    auto cre = rx::compile("CRITICAL[0-9]+");
    std::regex sre("CRITICAL[0-9]+", std::regex::optimize);
#if HAVE_BOOST
    boost::regex bre("CRITICAL[0-9]+");
#endif
    for (std::size_t sz : {1u<<10, 1u<<14, 1u<<18, 1u<<20, 1u<<24}) {
        std::string hay = make_log(sz); std::string_view H = hay; double mb = hay.size()/1e6;
        volatile bool sink=false; (void)rx::search(cre,H);
        double ct = best_ms([&]{ sink = rx::search(cre, H); });
        double st = best_ms([&]{ sink = std::regex_search(hay, sre); });
        double bt = 0;
#if HAVE_BOOST
        try { bt = best_ms([&]{ sink = boost::regex_search(hay, bre); }); } catch(...) { bt = -1; }
#endif
        (void)sink;
        printf("%8zuK %8.3f %8.3f", hay.size()/1024, ct, st);
        if (HAVE_BOOST) { if(bt<0) printf("    THREW"); else printf(" %8.3f", bt); }
        printf("   %7.0f  %5.1fx", mb/(ct/1e3), st/ct);
        if (HAVE_BOOST && bt>0) printf("    %5.1fx", bt/ct);
        printf("\n");
    }

    // (2) INPUT SHAPE — 4 MB, match at start / end / absent / everywhere.
    printf("\n== input shape: 4 MB, \"user=[a-z]+\" (search = leftmost, so position matters) ==\n");
    printf("%-12s %9s %9s%s\n", "shape", "cheatah", "std", HAVE_BOOST?"     boost":"");
    auto p2 = rx::compile("user=[a-z]+"); std::regex s2("user=[a-z]+", std::regex::optimize);
#if HAVE_BOOST
    boost::regex b2("user=[a-z]+");
#endif
    struct Shape { const char* name; std::string hay; };
    std::vector<Shape> shapes;
    shapes.push_back({"match@start", "user=zzz " + make_log(4'000'000)});
    shapes.push_back({"match@end  ", make_log(4'000'000) + " user=zzz"});
    { std::string h=make_log(4'000'000); for(auto&c:h) if(c=='u') c='x'; shapes.push_back({"absent     ", h}); }
    shapes.push_back({"everywhere ", make_log(4'000'000)});
    for (auto& sh : shapes) {
        std::string_view H = sh.hay; volatile bool sink=false; (void)rx::search(p2,H);
        double ct=best_ms([&]{ sink=rx::search(p2,H); });
        double st=best_ms([&]{ sink=std::regex_search(sh.hay,s2); });
        printf("%-12s %8.3f %8.3f", sh.name, ct, st);
#if HAVE_BOOST
        double bt=-1; try { bt=best_ms([&]{ sink=boost::regex_search(sh.hay,b2); }); } catch(...) {}
        if(bt<0) printf("    THREW"); else printf(" %8.3f", bt);
#endif
        (void)sink; printf("\n");
    }

    // (3) ADVERSARIAL — catastrophic backtracking. DFA is immune (linear).
    printf("\n== adversarial: (a+)+$ on 'a'*N + '!' — backtrackers go exponential ==\n");
    auto pbad = rx::compile("(a+)+$"); std::regex sbad("(a+)+$");
#if HAVE_BOOST
    boost::regex bbad("(a+)+$");
#endif
    for (int N : {16, 20, 24, 28}) {
        std::string bad(N,'a'); bad += '!';
        double ct=best_ms([&]{ volatile bool s=rx::search(pbad,bad); (void)s; },3);
        double st=best_ms([&]{ volatile bool s=std::regex_search(bad,sbad); (void)s; },1);
        printf("  N=%-3d cheatah=%.4fms  std=%9.2fms (%.0fx)", N, ct, st, st/ct);
#if HAVE_BOOST
        double bt=-1; try { bt=best_ms([&]{ volatile bool s=boost::regex_search(bad,bbad); (void)s; },1); } catch(...) {}
        if(bt<0) printf("  boost=THREW (complexity guard: refuses to match)");
        else printf("  boost=%9.2fms (%.0fx)", bt, bt/ct);
#endif
        printf("\n");
    }

    // (4) ADVERSARIALLY LARGE benign input — 64 MB, absent pattern (linear-time proof).
    printf("\n== adversarially large: 64 MB, absent pattern (single-pass, no backtracking) ==\n");
    std::string huge = make_log(64'000'000); std::string_view HG=huge;
    auto pl = rx::compile("NOSUCHTOKEN_[0-9]+"); (void)rx::search(pl,HG);
    double ct=best_ms([&]{ volatile bool s=rx::search(pl,HG); (void)s; },3);
    printf("  cheatah: %.1f ms (%.0f MB/s)\n", ct, (huge.size()/1e6)/(ct/1e3));
    return 0;
}
