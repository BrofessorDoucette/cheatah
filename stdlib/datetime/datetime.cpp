// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#include "datetime.hpp"

#include <chrono>
#include <ctime>

namespace cheatah::datetime {

namespace {
std::tm local_tm(double epoch) {
    const auto t = static_cast<std::time_t>(epoch);
    std::tm out{};
    localtime_r(&t, &out);
    return out;
}
std::string strf(const std::tm& tm, const char* fmt) {
    char buf[128];
    const std::size_t n = std::strftime(buf, sizeof(buf), fmt, &tm);
    return {buf, n};
}
} // namespace

double timestamp() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}
std::string now() { return strf(local_tm(timestamp()), "%Y-%m-%d %H:%M:%S"); }
std::string utcnow() {
    const auto t = static_cast<std::time_t>(timestamp());
    std::tm out{};
    gmtime_r(&t, &out);
    return strf(out, "%Y-%m-%dT%H:%M:%SZ");
}
std::string today() { return strf(local_tm(timestamp()), "%Y-%m-%d"); }

std::string format(double epoch, std::string_view fmt) {
    return strf(local_tm(epoch), std::string(fmt).c_str());
}

int year(double e) { return local_tm(e).tm_year + 1900; }
int month(double e) { return local_tm(e).tm_mon + 1; }
int day(double e) { return local_tm(e).tm_mday; }
int hour(double e) { return local_tm(e).tm_hour; }
int minute(double e) { return local_tm(e).tm_min; }
int second(double e) { return local_tm(e).tm_sec; }
int weekday(double e) { return (local_tm(e).tm_wday + 6) % 7; }  // Sun=0 -> Mon=0

} // namespace cheatah::datetime
