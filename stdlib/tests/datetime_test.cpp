#include "datetime.hpp"

#include <cstdlib>
#include <ctime>
#include <string>

#include <gtest/gtest.h>

namespace dt = cheatah::datetime;

namespace {
// Components use local time; pin the process timezone to UTC so they're deterministic.
void use_utc() {
    setenv("TZ", "UTC", 1);
    tzset();
}
}  // namespace

TEST(CheatahDatetime, ComponentsOfKnownEpoch) {
    use_utc();
    const double e = 1609459200.0;  // 2021-01-01 00:00:00 UTC
    EXPECT_EQ(dt::year(e), 2021);
    EXPECT_EQ(dt::month(e), 1);
    EXPECT_EQ(dt::day(e), 1);
    EXPECT_EQ(dt::hour(e), 0);
    EXPECT_EQ(dt::minute(e), 0);
    EXPECT_EQ(dt::second(e), 0);
    EXPECT_EQ(dt::weekday(e), 4);  // 2021-01-01 was a Friday (Mon=0 … Sun=6)
}

TEST(CheatahDatetime, EpochZero) {
    use_utc();
    EXPECT_EQ(dt::year(0.0), 1970);
    EXPECT_EQ(dt::month(0.0), 1);
    EXPECT_EQ(dt::day(0.0), 1);
    EXPECT_EQ(dt::weekday(0.0), 3);  // 1970-01-01 was a Thursday
}

TEST(CheatahDatetime, TimeOfDayComponents) {
    use_utc();
    const double e = 1609459200.0 + 13 * 3600 + 37 * 60 + 5;  // 13:37:05 UTC
    EXPECT_EQ(dt::hour(e), 13);
    EXPECT_EQ(dt::minute(e), 37);
    EXPECT_EQ(dt::second(e), 5);
}

TEST(CheatahDatetime, Format) {
    use_utc();
    const double e = 1609459200.0;
    EXPECT_EQ(dt::format(e, "%Y-%m-%d"), "2021-01-01");
    EXPECT_EQ(dt::format(e, "%H:%M:%S"), "00:00:00");
}

TEST(CheatahDatetime, NowTodayUtcnowTimestamp) {
    EXPECT_GT(dt::timestamp(), 1.6e9);   // well after 2020
    EXPECT_EQ(dt::now().size(), 19u);    // "YYYY-MM-DD HH:MM:SS"
    EXPECT_EQ(dt::today().size(), 10u);  // "YYYY-MM-DD"
    const std::string u = dt::utcnow();  // "YYYY-MM-DDTHH:MM:SSZ"
    ASSERT_EQ(u.size(), 20u);
    EXPECT_EQ(u.back(), 'Z');
    EXPECT_EQ(u[10], 'T');
}
