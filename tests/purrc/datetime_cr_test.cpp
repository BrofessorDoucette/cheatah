// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
// Compile-run unit tests for the `datetime` module: one test per function. Each
// writes a tiny .purr that calls a single datetime function, compiles it with
// purrc, runs it under the cheatah runtime, and asserts the exact stdout.
// Complements the in-process unit tests (stdlib/tests/datetime_test.cpp) and the
// per-module system-level test (StdlibE2E.Datetime).
//
// Component getters and `format` use local time, so they are pinned to a FIXED
// epoch (0.0 = 1970-01-01 UTC) and run under TZ=UTC (passed as the 4th
// expect_e2e arg) to make their output deterministic. The current-time
// functions (timestamp/now/utcnow/today) are non-deterministic, so they assert
// a DETERMINISTIC property (sign / length) instead of a raw value.
#include "e2e_harness.hpp"

TEST(DatetimeCompileRun, Timestamp) {
    e2e::expect_e2e("datetime_timestamp", R"PURR(import io
import datetime
io.print(datetime.timestamp() > 0.0)
)PURR", "True\n");
}

TEST(DatetimeCompileRun, Now) {
    // "YYYY-MM-DD HH:MM:SS" is always 19 characters.
    e2e::expect_e2e("datetime_now", R"PURR(import io
import datetime
io.print(len(datetime.now()) == 19)
)PURR", "True\n");
}

TEST(DatetimeCompileRun, Utcnow) {
    // "YYYY-MM-DDTHH:MM:SSZ" is always 20 characters.
    e2e::expect_e2e("datetime_utcnow", R"PURR(import io
import datetime
io.print(len(datetime.utcnow()) == 20)
)PURR", "True\n");
}

TEST(DatetimeCompileRun, Today) {
    // "YYYY-MM-DD" is always 10 characters.
    e2e::expect_e2e("datetime_today", R"PURR(import io
import datetime
io.print(len(datetime.today()) == 10)
)PURR", "True\n");
}

TEST(DatetimeCompileRun, Format) {
    e2e::expect_e2e("datetime_format", R"PURR(import io
import datetime
io.print(datetime.format(0.0, "%Y-%m-%d %H:%M:%S"))
)PURR", "1970-01-01 00:00:00\n", "TZ=UTC ");
}

TEST(DatetimeCompileRun, Year) {
    e2e::expect_e2e("datetime_year", R"PURR(import io
import datetime
io.print(datetime.year(0.0))
)PURR", "1970\n", "TZ=UTC ");
}

TEST(DatetimeCompileRun, Month) {
    e2e::expect_e2e("datetime_month", R"PURR(import io
import datetime
io.print(datetime.month(0.0))
)PURR", "1\n", "TZ=UTC ");
}

TEST(DatetimeCompileRun, Day) {
    e2e::expect_e2e("datetime_day", R"PURR(import io
import datetime
io.print(datetime.day(0.0))
)PURR", "1\n", "TZ=UTC ");
}

TEST(DatetimeCompileRun, Hour) {
    e2e::expect_e2e("datetime_hour", R"PURR(import io
import datetime
io.print(datetime.hour(0.0))
)PURR", "0\n", "TZ=UTC ");
}

TEST(DatetimeCompileRun, Minute) {
    e2e::expect_e2e("datetime_minute", R"PURR(import io
import datetime
io.print(datetime.minute(0.0))
)PURR", "0\n", "TZ=UTC ");
}

TEST(DatetimeCompileRun, Second) {
    e2e::expect_e2e("datetime_second", R"PURR(import io
import datetime
io.print(datetime.second(0.0))
)PURR", "0\n", "TZ=UTC ");
}

TEST(DatetimeCompileRun, Weekday) {
    // 1970-01-01 is a Thursday => 3 under Python's Monday=0 convention.
    e2e::expect_e2e("datetime_weekday", R"PURR(import io
import datetime
io.print(datetime.weekday(0.0))
)PURR", "3\n", "TZ=UTC ");
}
