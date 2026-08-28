// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
//
// bench_main — the entry point for cheatah_benchmarks, replacing benchmark::benchmark_main.
//
// It exists for the three things a Google Benchmark flag cannot express:
//
//   1. PROVENANCE. A published number is worthless without the machine, commit, compiler,
//      competitor versions, and harness settings that produced it. Those go into the
//      benchmark's own context (so they land in --benchmark_out_format=json) and into the
//      stamp block of any generated Markdown table.
//   2. A PUBLISHABILITY VERDICT. Repetitions stay a FLAG rather than a per-registration
//      ->Repetitions() call, because Benchmark::Repetitions() overrides the flag and would
//      make scripts/bench_smoke.sh — the fast pass inside the QA gate, whose timings are
//      explicitly discarded — pay for statistics it throws away. The cost of that choice is
//      that someone can run this binary bare and read the numbers as if they were rigorous.
//      So the binary judges its own run: fewer than kMinPublishReps repetitions, or no
//      interleaving, or too short a min_time, and it says so on stderr and records
//      cheatah_publishable=false where a script can see it.
//   3. THE COMPARISON TABLE. Google Benchmark reports rows; it has no idea that
//      BM_dot_cheatah and BM_dot_eigen are two sides of one claim. bench_pairs.hpp supplies
//      that, and CHEATAH_BENCH_TABLE=<path> writes the paired result out as stamped
//      Markdown — the same trick stdlib/regex/bench/rxbench.cpp already uses, generalized.
//
// Everything here is additive: the banner goes to stderr only, the exit status is
// unchanged, and stdout stays byte-for-byte what the default main would have produced, so
// scripts/bench_smoke.sh (greps stdout for ^BM_) and scripts/bench_gate.sh (parses stdout
// CSV) are unaffected.
#include <benchmark/benchmark.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "bench_labels.hpp"
#include "bench_pairs.hpp"

#if __has_include(<Eigen/Core>)
#include <Eigen/Core>
#define CHEATAH_BENCH_HAVE_EIGEN 1
#endif
#if __has_include(<glm/glm.hpp>)
#include <glm/glm.hpp>
#define CHEATAH_BENCH_HAVE_GLM 1
#endif
#ifdef CHEATAH_HAVE_OPENSSL
#include <openssl/crypto.h>
#include <openssl/opensslv.h>
#endif

namespace {

// A run must clear all three to be publishable. The repetition floor is 5 because that is
// the smallest odd count where a median is not just "the middle of three"; the min_time
// floor keeps a case from being timed inside one scheduler slice.
constexpr int kMinPublishReps = 5;
constexpr double kMinPublishMinTimeSec = 0.2;

// The same band scripts/bench_gate.sh uses, for the same reason: a ratio on a
// sub-nanosecond operation is noise, so a difference must clear BOTH a relative and an
// absolute floor before it is called a win or a loss.
constexpr double kThreshold = 1.15;
constexpr double kMinGapNs = 0.25;

// ---- what the run was asked to do ---------------------------------------------------
struct RunSettings {
    int reps = 1;
    double min_time_sec = 0.0;  // 0 = flag absent, i.e. Google Benchmark's own default
    bool interleaved = false;
    bool aggregates_only = false;
    bool listing_only = false;  // --benchmark_list_tests: enumerates names, measures nothing
};

// Scanned from argv BEFORE benchmark::Initialize, which consumes the flags it recognizes.
RunSettings scan_settings(int argc, char** argv) {
    RunSettings s;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (std::strncmp(a, "--benchmark_repetitions=", 24) == 0) {
            s.reps = std::atoi(a + 24);  // NOLINT(cert-err34-c): a benchmark flag we control, not untrusted input
        } else if (std::strncmp(a, "--benchmark_min_time=", 21) == 0) {
            s.min_time_sec = std::strtod(a + 21, nullptr);  // "0.2s" -> 0.2; "10x" -> 10
            if (std::strchr(a + 21, 'x') != nullptr) s.min_time_sec = 0.0;  // iteration form
        } else if (std::strncmp(a, "--benchmark_enable_random_interleaving=", 39) == 0) {
            s.interleaved = std::strcmp(a + 39, "true") == 0 || std::strcmp(a + 39, "1") == 0;
        } else if (std::strncmp(a, "--benchmark_report_aggregates_only=", 35) == 0) {
            s.aggregates_only = std::strcmp(a + 35, "true") == 0 || std::strcmp(a + 35, "1") == 0;
        } else if (std::strncmp(a, "--benchmark_list_tests", 22) == 0) {
            s.listing_only = std::strstr(a, "=false") == nullptr;
        }
    }
    return s;
}

// benchmark::Initialize REMOVES the flags it recognizes from argv, so the reproduction
// command has to be captured before it runs. A stamp that cannot tell you how to reproduce
// the number beside it is only half a stamp.
std::vector<std::string> g_argv;

void remember_argv(int argc, char** argv) {
    for (int i = 0; i < argc; ++i) g_argv.emplace_back(argv[i]);
}

bool publishable(const RunSettings& s) {
    return s.reps >= kMinPublishReps && s.interleaved && s.min_time_sec >= kMinPublishMinTimeSec;
}

// ---- provenance ---------------------------------------------------------------------
// The commit arrives by ENVIRONMENT, not a compile definition. A -D baked in at configure
// time goes stale the moment you commit without reconfiguring, and a stamp that quietly
// names the wrong commit is worse than one that admits it does not know. scripts/bench_run.sh
// sets these; a bare invocation honestly reports "unknown".
std::string env_or(const char* key, const char* fallback) {
    const char* v = std::getenv(key);
    return (v != nullptr && v[0] != '\0') ? std::string(v) : std::string(fallback);
}

std::string compiler_id() {
#if defined(__clang__)
    return "Clang " __clang_version__;
#elif defined(__GNUC__)
    return "GCC " __VERSION__;
#else
    return "unknown";
#endif
}

std::string competitor_versions() {
    std::string v;
    auto add = [&v](const std::string& s) {
        if (!v.empty()) v += ", ";
        v += s;
    };
#ifdef CHEATAH_BENCH_HAVE_EIGEN
    add("Eigen " + std::to_string(EIGEN_WORLD_VERSION) + "." + std::to_string(EIGEN_MAJOR_VERSION) +
        "." + std::to_string(EIGEN_MINOR_VERSION));
#endif
#ifdef CHEATAH_BENCH_HAVE_GLM
    add(std::string("GLM ") + GLM_VERSION_MESSAGE);
#endif
#ifdef CHEATAH_HAVE_OPENSSL
    add(std::string(OpenSSL_version(OPENSSL_VERSION)));
#endif
    return v.empty() ? "none linked" : v;
}

std::string today() {
    const std::time_t now = std::time(nullptr);
    char buf[32] = "unknown";
    if (const std::tm* tm = std::localtime(&now)) static_cast<void>(std::strftime(buf, sizeof buf, "%Y-%m-%d", tm));
    return buf;
}

std::string harness_line(const RunSettings& s) {
    char buf[192];
    static_cast<void>(std::snprintf(buf, sizeof buf, "reps=%d, min_time=%.3gs, random-interleaving=%s", s.reps,
                  s.min_time_sec, s.interleaved ? "on" : "off"));
    return buf;
}

// ---- collecting the runs ------------------------------------------------------------
struct CaseTimes {
    std::vector<double> raw_ns;   // one per repetition, when they are reported
    double median_ns = 0.0;       // the median aggregate, when Google Benchmark reports one
    double stddev_ns = -1.0;      // the stddev aggregate, when present
    double bytes_per_sec = 0.0;   // set only by throughput benchmarks
    double items_per_sec = 0.0;   // SetItemsProcessed — p256 counts operations, not bytes
};

double median_of(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const std::size_t n = v.size();
    return (n % 2) ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

// Inter-quartile range: the dispersion we publish. Chosen over stddev because a benchmark
// sample is not normal — one descheduled repetition drags a stddev far more than it drags
// the middle 50%.
double iqr_of(std::vector<double> v) {
    if (v.size() < 4) return -1.0;
    std::sort(v.begin(), v.end());
    auto quantile = [&v](double q) {
        const double pos = q * static_cast<double>(v.size() - 1);
        const auto lo = static_cast<std::size_t>(pos);
        const std::size_t hi = std::min(lo + 1, v.size() - 1);
        return v[lo] + (pos - static_cast<double>(lo)) * (v[hi] - v[lo]);
    };
    return quantile(0.75) - quantile(0.25);
}

// Forwards every call to the real (format-aware) display reporter, so stdout stays exactly
// what --benchmark_format=csv|json|console would have produced, while keeping a copy of the
// timings for the paired table.
class PairTally : public benchmark::BenchmarkReporter {
  public:
    explicit PairTally(benchmark::BenchmarkReporter* inner) : inner_(inner) {}

    bool ReportContext(const Context& context) override {
        host_ = context.sys_info.name;
        num_cpus_ = context.cpu_info.num_cpus;
        mhz_ = context.cpu_info.cycles_per_second / 1e6;
        switch (context.cpu_info.scaling) {
            case benchmark::CPUInfo::ENABLED: scaling_ = "enabled"; break;
            case benchmark::CPUInfo::DISABLED: scaling_ = "disabled"; break;
            default: scaling_ = "unknown"; break;
        }
        return inner_->ReportContext(context);
    }

    void ReportRunsConfig(double min_time, bool has_explicit_iters,
                          benchmark::IterationCount iters) override {
        inner_->ReportRunsConfig(min_time, has_explicit_iters, iters);
    }

    void ReportRuns(const std::vector<Run>& report) override {
        for (const Run& r : report) {
            if (r.skipped) continue;
            CaseTimes& c = cases_[r.run_name.str()];
            const bool is_agg = r.run_type == Run::RT_Aggregate;

            // SetBytesProcessed lands in the counters map, not a named field — and every
            // aggregate row carries one, including `stddev` and `cv`. Taking the last one
            // seen would report the standard deviation of the throughput as the throughput.
            // Only a real measurement (a raw run, or the median aggregate) may set it.
            if (!is_agg || r.aggregate_name == "median") {
                if (const auto it = r.counters.find("bytes_per_second"); it != r.counters.end())
                    if (it->second.value > 0.0) c.bytes_per_sec = it->second.value;
                // p256_bench.cpp uses SetItemsProcessed, not SetBytesProcessed — a signing
                // operation has no meaningful byte count. Without this the `solo` layout's
                // throughput column would be silently empty for the one suite that needs it.
                if (const auto it = r.counters.find("items_per_second"); it != r.counters.end())
                    if (it->second.value > 0.0) c.items_per_sec = it->second.value;
            }

            if (is_agg) {
                if (r.aggregate_name == "median") c.median_ns = r.GetAdjustedRealTime();
                if (r.aggregate_name == "stddev") c.stddev_ns = r.GetAdjustedRealTime();
            } else {
                c.raw_ns.push_back(r.GetAdjustedRealTime());
            }
        }
        inner_->ReportRuns(report);
    }

    void Finalize() override { inner_->Finalize(); }

    const std::map<std::string, CaseTimes>& cases() const { return cases_; }
    std::string host_line() const {
        char buf[256];
        static_cast<void>(std::snprintf(buf, sizeof buf, "%s, %d CPUs @ %.0f MHz", host_.c_str(), num_cpus_, mhz_));
        return buf;
    }
    const std::string& scaling() const { return scaling_; }

  private:
    benchmark::BenchmarkReporter* inner_;
    std::map<std::string, CaseTimes> cases_;
    std::string host_ = "unknown";
    std::string scaling_ = "unknown";
    int num_cpus_ = 0;
    double mhz_ = 0.0;
};

std::string fmt_ns(double ns) {
    char buf[32];
    if (ns >= 1e6) static_cast<void>(std::snprintf(buf, sizeof buf, "%.2f ms", ns / 1e6));
    else if (ns >= 1e3) static_cast<void>(std::snprintf(buf, sizeof buf, "%.2f µs", ns / 1e3));
    else static_cast<void>(std::snprintf(buf, sizeof buf, "%.2f ns", ns));
    return buf;
}

// The dispersion we can honestly report for a case. IQR is preferred — a benchmark sample
// is not normal, and one descheduled repetition moves a standard deviation far more than it
// moves the middle 50%. But --benchmark_report_aggregates_only (what both gates pass)
// suppresses the raw per-repetition rows, leaving only Google Benchmark's own stddev. Fall
// back to that rather than print nothing, and label which one it is.
struct Spread {
    double value = -1.0;
    const char* kind = "";
};

Spread spread_of(const CaseTimes& c) {
    if (const double q = iqr_of(c.raw_ns); q >= 0.0) return {q, "IQR"};
    if (c.stddev_ns >= 0.0) return {c.stddev_ns, "sd"};
    return {};
}

std::string fmt_disp(const Spread& s) {
    if (s.value < 0.0) return "—";
    return "±" + fmt_ns(s.value) + " " + s.kind;
}

// >1 means cheatah is faster. The verdict uses the same two-floor band as bench_gate.sh.
std::string verdict(double ours, double rival) {
    if (rival > ours * kThreshold && (rival - ours) > kMinGapNs) return "faster";
    if (ours > rival * kThreshold && (ours - rival) > kMinGapNs) return "**slower**";
    return "parity";
}

// ---- layouts -------------------------------------------------------------------------
//
// One suite, one shape. Rather than let each document invent a table and be hand-typed into
// place (which is how every published table in this repo ended up unreproducible), the
// FORMATTER lives here and the document is generated from it. Selected by
// CHEATAH_BENCH_LAYOUT; unset means `pairs`, whose bytes are unchanged, so scripts/bench_gate.sh
// and ad-hoc runs are unaffected.
enum class Layout { Pairs, OpsType, Throughput, Solo, Highlights };

Layout layout_from_env() {
    const std::string v = env_or("CHEATAH_BENCH_LAYOUT", "pairs");
    if (v == "pairs") return Layout::Pairs;
    if (v == "opstype") return Layout::OpsType;
    if (v == "throughput") return Layout::Throughput;
    if (v == "solo") return Layout::Solo;
    if (v == "highlights") return Layout::Highlights;
    // Never silently fall back to a different shape than asked for: that would publish a
    // table nobody reviewed under a stamp claiming it was the one they did.
    static_cast<void>(std::fprintf(stderr, "bench_main: unknown CHEATAH_BENCH_LAYOUT '%s' — using `pairs`\n",
                 v.c_str()));
    return Layout::Pairs;
}

// The command that reproduces this table, rebuilt from the argv captured before
// benchmark::Initialize ate it, plus the CHEATAH_BENCH_* variables that actually shaped it.
std::string reproduction_command() {
    std::vector<std::string> lines;
    for (const char* key : {"CHEATAH_BENCH_SUITE", "CHEATAH_BENCH_LAYOUT", "CHEATAH_BENCH_ROWS",
                            "CHEATAH_BENCH_WATCH"}) {
        const std::string v = env_or(key, "");
        if (!v.empty()) lines.push_back(std::string(key) + "='" + v + "'");
    }
    std::string argv_line;
    for (std::size_t i = 0; i < g_argv.size(); ++i) {
        argv_line += g_argv[i];
        if (i + 1 < g_argv.size()) argv_line += " ";
    }
    // CHEATAH_BENCH_TABLE is deliberately omitted: it is where THIS run happened to write,
    // not part of what the table measures, and baking a /tmp path into a published doc is
    // noise. The reader supplies their own output path.
    if (!argv_line.empty()) lines.push_back(argv_line);
    std::string cmd;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        cmd += lines[i];
        if (i + 1 < lines.size()) cmd += " \\\n           ";
    }
    return cmd;
}

// `opstype` prints bare numbers — the unit is in the column header and the spread kind is in
// the summary line, so fmt_ns/fmt_disp (which carry both inline) cannot be reused there.
std::string fmt_num2(double ns) {
    char buf[32];
    static_cast<void>(std::snprintf(buf, sizeof buf, "%.2f", ns));
    return buf;
}

std::string fmt_pm2(const Spread& s) {
    if (s.value < 0.0) return "—";
    char buf[32];
    static_cast<void>(std::snprintf(buf, sizeof buf, "±%.2f", s.value));
    return buf;
}

std::string fmt_ratio(double r) {
    char buf[32];
    static_cast<void>(std::snprintf(buf, sizeof buf, "%.2f\u00d7", r));  // U+00D7 MULTIPLICATION SIGN, not 'x'
    return buf;
}

// Bytes where the benchmark counted bytes, operations where it counted operations.
std::string fmt_rate(double bps, double ips) {
    char buf[48];
    if (bps > 0.0) {
        static_cast<void>(std::snprintf(buf, sizeof buf, "%.2f GiB/s", bps / (1024.0 * 1024.0 * 1024.0)));
    } else if (ips > 0.0) {
        if (ips >= 1000.0) static_cast<void>(std::snprintf(buf, sizeof buf, "%.1f k/s", ips / 1000.0));
        else static_cast<void>(std::snprintf(buf, sizeof buf, "%.0f /s", ips));
    } else {
        return "—";
    }
    return buf;
}

// A pair key BM_<op>_<type> splits at the LAST underscore, so BM_distance2_vec4f is
// {distance2, vec4f}. A key with nothing to split lands in `Other` rather than vanishing —
// a silently dropped row is how a published tally goes quietly wrong.
struct OpType {
    std::string op, type;
};

OpType split_op_type(const std::string& key) {
    std::string stem = key;
    if (stem.rfind("BM_", 0) == 0) stem = stem.substr(3);
    const std::size_t u = stem.rfind('_');
    if (u == std::string::npos) return {stem, ""};
    return {stem.substr(0, u), stem.substr(u + 1)};
}

const char* emoji_verdict(double ours, double rival) {
    const std::string v = verdict(ours, rival);
    if (v == "faster") return "\U0001F7E2 faster";
    if (v == "parity") return "\u2B1C parity";
    return "\U0001F534 slower";
}

// For THROUGHPUT, higher is better, so the band test runs on the rates rather than on times.
std::string prose_gap(double ours_bps, double rival_bps) {
    if (ours_bps <= 0.0 || rival_bps <= 0.0) return "—";
    char buf[64];
    if (ours_bps > rival_bps * kThreshold) {
        static_cast<void>(std::snprintf(buf, sizeof buf, "**%.2f\u00d7 faster**", ours_bps / rival_bps));
    } else if (rival_bps > ours_bps * kThreshold) {
        static_cast<void>(std::snprintf(buf, sizeof buf, "%.2f\u00d7 slower", rival_bps / ours_bps));
    } else {
        static_cast<void>(std::snprintf(buf, sizeof buf, "**parity** (%.2f\u00d7)",
                      ours_bps > rival_bps ? ours_bps / rival_bps : rival_bps / ours_bps));
    }
    return buf;
}

// CHEATAH_BENCH_ROWS='key=label;key2=label2' — curation lives in the INVOCATION, which the
// stamp records verbatim, rather than in a Markdown edit nobody can reproduce. Order is
// curation too, so rows emit in the order given.
std::vector<std::pair<std::string, std::string>> parse_row_spec(const std::string& spec) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t i = 0;
    while (i < spec.size()) {
        std::size_t semi = spec.find(';', i);
        if (semi == std::string::npos) semi = spec.size();
        const std::string entry = spec.substr(i, semi - i);
        if (!entry.empty()) {
            const std::size_t eq = entry.find('=');
            if (eq == std::string::npos) out.emplace_back(entry, entry);
            else out.emplace_back(entry.substr(0, eq), entry.substr(eq + 1));
        }
        i = semi + 1;
    }
    return out;
}

// ---- the paired Markdown table ------------------------------------------------------
struct PairedRow {
    std::string key;
    std::string rival_label;
    double ours_ns = 0.0, rival_ns = 0.0;
    Spread ours_spread, rival_spread;
    double ours_bps = 0.0, rival_bps = 0.0;
};

std::string fmt_gibs(double bytes_per_sec) {
    char buf[32];
    static_cast<void>(std::snprintf(buf, sizeof buf, "%.2f GiB/s", bytes_per_sec / (1024.0 * 1024.0 * 1024.0)));
    return buf;
}

// One case -> one number: the median aggregate when Google Benchmark computed one, else the
// median of the repetitions we saw.
struct Resolved {
    std::map<std::string, double> ns, bps, ips;
    std::map<std::string, Spread> spread;
};

Resolved resolve(const PairTally& tally) {
    Resolved r;
    for (const auto& [name, c] : tally.cases()) {
        r.ns[name] = (c.median_ns > 0.0) ? c.median_ns : median_of(c.raw_ns);
        r.spread[name] = spread_of(c);
        r.bps[name] = c.bytes_per_sec;
        r.ips[name] = c.items_per_sec;
    }
    return r;
}

struct SoloRow {
    std::string key;
    double ns = 0.0;
    Spread spread;
    double bps = 0.0, ips = 0.0;
};

// Every case, unpaired. The invocation is expected to carry a filter; a layout whose
// contract is "no rival" should not silently hide rows it was handed.
std::vector<SoloRow> collect_solo(const Resolved& res) {
    std::vector<SoloRow> rows;
    for (const auto& [name, t] : res.ns)
        rows.push_back({name, t, res.spread.at(name), res.bps.at(name), res.ips.at(name)});
    return rows;
}

std::vector<PairedRow> collect_pairs(const Resolved& res) {
    const auto& ns = res.ns;
    const auto& bps = res.bps;
    const auto& spread = res.spread;

    // Group by pair key. `ours` and the rivals of one key land in the same slot.
    struct Slot {
        std::string ours_name;
        std::map<std::string, std::string> rivals;  // label -> benchmark name
    };
    std::map<std::string, Slot> slots;
    for (const auto& [name, _] : ns) {
        const cheatah::bench::Pairing p = cheatah::bench::classify_benchmark(name);
        if (p.side == cheatah::bench::Side::Ours) slots[p.key].ours_name = name;
        else if (p.side == cheatah::bench::Side::Rival) slots[p.key].rivals[p.label] = name;
    }

    std::vector<PairedRow> rows;
    for (const auto& [key, slot] : slots) {
        if (slot.ours_name.empty()) continue;  // a rival with no cheatah twin proves nothing
        for (const auto& [label, rival_name] : slot.rivals) {
            PairedRow r;
            r.key = key;
            r.rival_label = label;
            r.ours_ns = ns.at(slot.ours_name);
            r.ours_spread = spread.at(slot.ours_name);
            r.rival_ns = ns.at(rival_name);
            r.rival_spread = spread.at(rival_name);
            r.ours_bps = bps.at(slot.ours_name);
            r.rival_bps = bps.at(rival_name);
            rows.push_back(r);
        }
    }

    return rows;
}

// The stamp. `layout:` and `watch:` are emitted ONLY when their env var is set, so a caller
// that sets neither (bench_gate.sh, an ad-hoc run) gets byte-for-byte what this file emitted
// before layouts existed. `note` is appended verbatim before the closing --> so a layout can
// add its own reproduction block.
void emit_stamp(std::FILE* f, const PairTally& tally, const RunSettings& settings,
                const std::string& note) {
    const bool ok = publishable(settings);
    static_cast<void>(std::fprintf(f,
                 "<!-- cheatah-bench-stamp v1\n"
                 "     suite:        %s\n"
                 "     generated:    %s\n"
                 "     commit:       %s\n"
                 "     host:         %s\n"
                 "     cpu-scaling:  %s\n"
                 "     build:        %s, Google Benchmark %s\n"
                 "     competitors:  %s\n"
                 "     harness:      %s\n"
                 "     statistic:    median real time per case; spread = IQR over\n"
                 "                   repetitions, or `sd` where\n"
                 "                   --benchmark_report_aggregates_only hid the raw runs\n"
                 "     publishable:  %s\n",
                 env_or("CHEATAH_BENCH_SUITE", "cheatah_benchmarks").c_str(), today().c_str(),
                 env_or("CHEATAH_BENCH_COMMIT", "unknown").c_str(), tally.host_line().c_str(),
                 tally.scaling().c_str(), compiler_id().c_str(),
                 benchmark::GetBenchmarkVersion().c_str(),
                 competitor_versions().c_str(), harness_line(settings).c_str(),
                 ok ? "true" : "false"));

    const std::string layout = env_or("CHEATAH_BENCH_LAYOUT", "");
    if (!layout.empty()) static_cast<void>(std::fprintf(f, "     layout:       %s\n", layout.c_str()));
    // `watch:` is what scripts/bench_table.purr uses to decide whether this table has gone
    // stale: it names the source paths whose change invalidates the measurement.
    const std::string watch = env_or("CHEATAH_BENCH_WATCH", "");
    if (!watch.empty()) static_cast<void>(std::fprintf(f, "     watch:        %s\n", watch.c_str()));
    if (!note.empty()) static_cast<void>(std::fprintf(f, "%s", note.c_str()));
    static_cast<void>(std::fprintf(f, "-->\n\n"));
}

void emit_pairs(std::FILE* f, const std::vector<PairedRow>& rows) {
    if (rows.empty()) {
        static_cast<void>(std::fprintf(f, "_No paired cases in this run._\n"));
        return;
    }

    static_cast<void>(std::fprintf(f, "| case | cheatah | spread | vs | rival | spread | ratio | verdict |\n"));
    static_cast<void>(std::fprintf(f, "|---|--:|--:|---|--:|--:|--:|---|\n"));
    for (const PairedRow& r : rows) {
        static_cast<void>(std::fprintf(f, "| %s | %s | %s | %s | %s | %s | %.2fx | %s |\n", r.key.c_str(),
                     fmt_ns(r.ours_ns).c_str(), fmt_disp(r.ours_spread).c_str(),
                     r.rival_label.c_str(), fmt_ns(r.rival_ns).c_str(),
                     fmt_disp(r.rival_spread).c_str(),
                     (r.ours_ns > 0.0) ? r.rival_ns / r.ours_ns : 0.0,
                     verdict(r.ours_ns, r.rival_ns).c_str()));
    }

    // Throughput rows, when the benchmark set bytes_per_second. Reported separately rather
    // than as another column: only some suites measure it, and a mostly-empty column reads
    // as missing data rather than as "not applicable".
    const bool any_bps = std::any_of(rows.begin(), rows.end(), [](const PairedRow& r) {
        return r.ours_bps > 0.0 && r.rival_bps > 0.0;
    });
    if (any_bps) {
        static_cast<void>(std::fprintf(f, "\n| case | cheatah | vs | rival | ratio |\n|---|--:|---|--:|--:|\n"));
        for (const PairedRow& r : rows) {
            if (r.ours_bps <= 0.0 || r.rival_bps <= 0.0) continue;
            static_cast<void>(std::fprintf(f, "| %s | %s | %s | %s | %.2fx |\n", r.key.c_str(),
                         fmt_gibs(r.ours_bps).c_str(), r.rival_label.c_str(),
                         fmt_gibs(r.rival_bps).c_str(), r.ours_bps / r.rival_bps));
        }
    }

    // The tally, per rival — the honest summary line, including losses.
    std::map<std::string, std::array<int, 3>> tally_by_rival;  // label -> {faster, parity, slower}
    for (const PairedRow& r : rows) {
        const std::string v = verdict(r.ours_ns, r.rival_ns);
        auto& t = tally_by_rival[r.rival_label];
        if (v == "faster") ++t[0];
        else if (v == "parity") ++t[1];
        else ++t[2];
    }
    static_cast<void>(std::fprintf(f, "\n**Tally** (a difference counts only above %.2fx AND %.2f ns) — ", kThreshold,
                 kMinGapNs));
    bool first = true;
    for (const auto& [label, t] : tally_by_rival) {
        static_cast<void>(std::fprintf(f, "%svs %s: **%d faster / %d parity / %d slower**", first ? "" : "; ",
                     label.c_str(), t[0], t[1], t[2]));
        first = false;
    }
    static_cast<void>(std::fprintf(f, ".\n"));

    for (const PairedRow& r : rows) {
        if (verdict(r.ours_ns, r.rival_ns) == "**slower**")
            static_cast<void>(std::fprintf(f, "- Loss vs %s: `%s` — cheatah %s vs %s (%.2fx slower)\n",
                         r.rival_label.c_str(), r.key.c_str(), fmt_ns(r.ours_ns).c_str(),
                         fmt_ns(r.rival_ns).c_str(), r.ours_ns / r.rival_ns));
    }

}

// ---- opstype: the fixarray-vs-GLM shape ----------------------------------------------
//
// RATIO DIRECTION: ours/rival, so LOWER IS BETTER here. That is the opposite of `pairs` and
// of `highlights` (both rival/ours, higher is better). The inversion is deliberate — this
// table's column header says "lower is better" — but it is a genuine trap, so if you touch
// one of the three, check the other two.
void emit_opstype(std::FILE* f, const std::vector<PairedRow>& rows, const RunSettings& settings) {
    if (rows.empty()) {
        static_cast<void>(std::fprintf(f, "_No paired cases in this run._\n"));
        return;
    }
    const char* kind = rows.front().ours_spread.kind;
    const std::string spread_word =
        (std::strcmp(kind, "IQR") == 0) ? "IQR" : "sample standard deviation";

    // No blank line between --> and <details>: the rendered page reads as one block.
    static_cast<void>(std::fprintf(f,
                 "<details><summary><b>Full %s comparison — all %zu operations</b> "
                 "(ns, lower is better; ± is the %s over %d interleaved repetitions)"
                 "</summary>\n\n",
                 cheatah::bench::display_label(rows.front().rival_label).c_str(), rows.size(),
                 spread_word.c_str(), settings.reps));

    struct Group {
        const char* heading;
        std::vector<const PairedRow*> rows;
    };
    Group vectors{"Vectors", {}}, matrices{"Matrices", {}}, other{"Other", {}};
    for (const PairedRow& r : rows) {
        const std::string t = split_op_type(r.key).type;
        if (t.rfind("mat", 0) == 0) matrices.rows.push_back(&r);
        else if (t.rfind("vec", 0) == 0) vectors.rows.push_back(&r);
        else other.rows.push_back(&r);
    }

    for (const Group* g : {&vectors, &matrices, &other}) {
        if (g->rows.empty()) continue;  // no stray heading over an empty section
        static_cast<void>(std::fprintf(f, "\n#### %s\n\n", g->heading));
        static_cast<void>(std::fprintf(f, "| operation | type | `Fixed` | %s | ratio | |\n",
                     cheatah::bench::display_label(g->rows.front()->rival_label).c_str()));
        static_cast<void>(std::fprintf(f, "|---|---|--:|--:|:--:|---|\n"));
        for (const PairedRow* r : g->rows) {
            const OpType ot = split_op_type(r->key);
            static_cast<void>(std::fprintf(f, "| `%s` | %s | %s %s | %s %s | %s | %s |\n", ot.op.c_str(),
                         ot.type.c_str(), fmt_num2(r->ours_ns).c_str(),
                         fmt_pm2(r->ours_spread).c_str(), fmt_num2(r->rival_ns).c_str(),
                         fmt_pm2(r->rival_spread).c_str(),
                         fmt_ratio(r->rival_ns > 0.0 ? r->ours_ns / r->rival_ns : 0.0).c_str(),
                         emoji_verdict(r->ours_ns, r->rival_ns)));
        }
    }

    int faster = 0, parity = 0, slower = 0;
    for (const PairedRow& r : rows) {
        const std::string v = verdict(r.ours_ns, r.rival_ns);
        if (v == "faster") ++faster;
        else if (v == "parity") ++parity;
        else ++slower;
    }
    static_cast<void>(std::fprintf(f, "\n**%d faster, %d at parity, %d slower** across %zu operations.\n\n",
                 faster, parity, slower, rows.size()));
    static_cast<void>(std::fprintf(f, "</details>\n"));
}

// ---- throughput: the crypto-vs-OpenSSL shape -----------------------------------------
//
// Rates, not times: for a cipher the interesting quantity is bytes per second, and the
// verdict band therefore runs on the RATES (higher better) rather than on the ns.
void emit_throughput(std::FILE* f, std::vector<PairedRow> rows) {
    // Only rows where both sides measured a rate. This is what keeps
    // BM_CryptoChaCha20Poly1305_Cheatah_Into — a variant of ours with no rival — out.
    rows.erase(std::remove_if(rows.begin(), rows.end(),
                              [](const PairedRow& r) {
                                  return r.ours_bps <= 0.0 || r.rival_bps <= 0.0;
                              }),
               rows.end());
    if (rows.empty()) {
        static_cast<void>(std::fprintf(f, "_No throughput pairs in this run._\n"));
        return;
    }
    // Best standing first: the reader should meet the honest headline, then the gaps.
    std::sort(rows.begin(), rows.end(), [](const PairedRow& a, const PairedRow& b) {
        return (a.rival_bps / a.ours_bps) < (b.rival_bps / b.ours_bps);
    });

    static_cast<void>(std::fprintf(f, "| Primitive | cheatah | %s | gap |\n|-----------|--------:|--------:|----:|\n",
                 cheatah::bench::display_label(rows.front().rival_label).c_str()));
    for (const PairedRow& r : rows) {
        const std::string gap = prose_gap(r.ours_bps, r.rival_bps);
        const bool losing = gap.find("slower") != std::string::npos;
        const std::string ours = fmt_rate(r.ours_bps, 0.0);
        static_cast<void>(std::fprintf(f, "| %s | %s%s%s | %s | %s |\n",
                     cheatah::bench::label_for(r.key).c_str(), losing ? "" : "**", ours.c_str(),
                     losing ? "" : "**", fmt_rate(r.rival_bps, 0.0).c_str(), gap.c_str()));
    }
}

// ---- solo: a suite with no rival (p256) ----------------------------------------------
void emit_solo(std::FILE* f, const std::vector<SoloRow>& rows) {
    if (rows.empty()) {
        static_cast<void>(std::fprintf(f, "_No cases in this run._\n"));
        return;
    }
    static_cast<void>(std::fprintf(f, "| Op | median | spread | throughput |\n|---|--:|--:|--:|\n"));
    for (const SoloRow& r : rows)
        static_cast<void>(std::fprintf(f, "| `%s` | %s | %s | %s |\n", r.key.c_str(), fmt_ns(r.ns).c_str(),
                     fmt_disp(r.spread).c_str(), fmt_rate(r.bps, r.ips).c_str()));
}

// ---- highlights: a curated subset, curated in the INVOCATION --------------------------
//
// RATIO DIRECTION: rival/ours, higher is better — the opposite of `opstype`. See the note
// there.
bool emit_highlights(std::FILE* f, const std::vector<PairedRow>& rows, const std::string& spec) {
    const auto wanted = parse_row_spec(spec);
    if (wanted.empty()) {
        static_cast<void>(std::fprintf(f, "_CHEATAH_BENCH_ROWS was empty — nothing to highlight._\n"));
        return false;
    }
    std::map<std::string, const PairedRow*> by_key;
    for (const PairedRow& r : rows) by_key[r.key] = &r;

    static_cast<void>(std::fprintf(f, "| operation | `Fixed` | %s | | |\n|-----------|--------:|----:|---|---|\n",
                 rows.empty() ? "rival"
                              : cheatah::bench::display_label(rows.front().rival_label).c_str()));
    bool complete = true;
    for (const auto& [key, label] : wanted) {
        const auto it = by_key.find(key);
        if (it == by_key.end()) {
            // Do NOT silently drop it: a curation spec naming a case that did not run is a
            // broken table, and the em-dashes plus publishable:false make the gate say so.
            static_cast<void>(std::fprintf(f, "| `%s` | — | — | — | not measured |\n", label.c_str()));
            static_cast<void>(std::fprintf(stderr, "bench_main: CHEATAH_BENCH_ROWS names '%s', which produced no "
                                 "measurement\n", key.c_str()));
            complete = false;
            continue;
        }
        const PairedRow* r = it->second;
        const std::string v = verdict(r->ours_ns, r->rival_ns);
        std::string note = v;
        if (v == "parity") {
            char buf[64];
            static_cast<void>(std::snprintf(buf, sizeof buf, "parity — gap %.2f ns",
                          r->rival_ns > r->ours_ns ? r->rival_ns - r->ours_ns
                                                   : r->ours_ns - r->rival_ns));
            note = buf;
        }
        const bool win = (v == "faster");
        static_cast<void>(std::fprintf(f, "| `%s` | %s%s%s %s | %s %s | %s | %s |\n", label.c_str(),
                     win ? "**" : "", fmt_ns(r->ours_ns).c_str(), win ? "**" : "",
                     fmt_pm2(r->ours_spread).c_str(), fmt_ns(r->rival_ns).c_str(),
                     fmt_pm2(r->rival_spread).c_str(),
                     fmt_ratio(r->ours_ns > 0.0 ? r->rival_ns / r->ours_ns : 0.0).c_str(),
                     note.c_str()));
    }
    return complete;
}

void write_table(const char* path, const PairTally& tally, const RunSettings& settings) {
    const Layout layout = layout_from_env();
    const Resolved res = resolve(tally);
    const std::vector<PairedRow> pairs = collect_pairs(res);

    std::FILE* f = std::fopen(path, "w");
    if (f == nullptr) {
        static_cast<void>(std::fprintf(stderr, "bench_main: cannot write table to %s\n", path));
        return;
    }

    // Every published table carries its reproduction command into the stamp, so it says exactly
    // how to regenerate itself — including the curation spec where a layout has one. This used to
    // skip the `pairs` layout, which left linalg-vs-eigen as the one table a reader could not
    // reproduce from what it printed.
    const std::string note = "\n     PRODUCED BY:\n       " + reproduction_command() + "\n";
    emit_stamp(f, tally, settings, note);

    switch (layout) {
        case Layout::Pairs: emit_pairs(f, pairs); break;
        case Layout::OpsType: emit_opstype(f, pairs, settings); break;
        case Layout::Throughput: emit_throughput(f, pairs); break;
        case Layout::Solo: emit_solo(f, collect_solo(res)); break;
        case Layout::Highlights:
            emit_highlights(f, pairs, env_or("CHEATAH_BENCH_ROWS", ""));
            break;
    }

    static_cast<void>(std::fclose(f));
    static_cast<void>(std::fprintf(stderr, "bench_main: table written to %s\n", path));
}

}  // namespace

int main(int argc, char** argv) {
    const RunSettings settings = scan_settings(argc, argv);
    remember_argv(argc, argv);  // before Initialize, which consumes the flags it recognizes

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;

    const bool ok = publishable(settings);
    benchmark::AddCustomContext("cheatah_publishable", ok ? "true" : "false");
    benchmark::AddCustomContext("cheatah_commit", env_or("CHEATAH_BENCH_COMMIT", "unknown"));
    benchmark::AddCustomContext("cheatah_compiler", compiler_id());
    benchmark::AddCustomContext("cheatah_competitors", competitor_versions());
    benchmark::AddCustomContext("cheatah_harness", harness_line(settings));
    benchmark::AddCustomContext("cheatah_statistic", "median real time; dispersion = IQR");

    // stderr only, and never fatal: a fast unpublishable run is a legitimate thing to want
    // (that is exactly what the QA gate's smoke pass is). What is not legitimate is reading
    // its numbers as a benchmark result, so say so where a human will see it.
    if (!ok && !settings.listing_only && std::getenv("CHEATAH_BENCH_SMOKE") == nullptr) {
        static_cast<void>(std::fprintf(stderr,
                     "\n[bench_main] NOT PUBLISHABLE — %s.\n"
                     "             These timings are indicative only. For numbers that may be "
                     "published,\n"
                     "             run through scripts/bench_run.sh publish (>=%d repetitions, "
                     ">=%.1fs min_time,\n"
                     "             random interleaving on).\n\n",
                     harness_line(settings).c_str(), kMinPublishReps, kMinPublishMinTimeSec));
    }

    // CreateDefaultDisplayReporter returns a leaked singleton — do NOT take ownership.
    PairTally tally(benchmark::CreateDefaultDisplayReporter());
    benchmark::RunSpecifiedBenchmarks(&tally);
    benchmark::Shutdown();

    if (const char* path = std::getenv("CHEATAH_BENCH_TABLE")) write_table(path, tally, settings);
    return 0;
}
