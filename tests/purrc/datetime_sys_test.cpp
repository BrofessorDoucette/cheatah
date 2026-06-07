// System-level test for the cheatah `datetime` module: a single cohesive .purr
// program that exercises EVERY public function in stdlib/datetime/datetime.hpp
// (timestamp, now, utcnow, today, format, year, month, day, hour, minute,
// second, weekday), compiles it with purrc into a loadable module, runs it
// under the cheatah runtime (TZ=UTC), and asserts the exact stdout.
//
// The current-time functions (timestamp/now/utcnow/today) are
// non-deterministic, so the program prints DETERMINISTIC PROPERTIES (sign /
// string length). The component getters and `format` use the calendar in local
// time, so they are pinned to a FIXED epoch (0.0 = 1970-01-01 00:00:00 UTC) and
// run under TZ=UTC (the 4th expect_e2e arg) to be fully deterministic.
// Complements the in-process unit tests (stdlib/tests/datetime_test.cpp) and
// the per-function compile-run tests (tests/purrc/datetime_cr_test.cpp).
#include "e2e_harness.hpp"

TEST(StdlibE2E, Datetime) {
    e2e::expect_e2e("datetime_sys", R"PURR(import io
import datetime

# Current-time functions are non-deterministic; assert stable properties.
io.print(datetime.timestamp() > 0.0)
io.print(len(datetime.now()) == 19)
io.print(len(datetime.utcnow()) == 20)
io.print(len(datetime.today()) == 10)

# Fixed epoch (0.0 = 1970-01-01 00:00:00 UTC) under TZ=UTC is fully deterministic.
io.print(datetime.format(0.0, "%Y-%m-%d %H:%M:%S"))
io.print(datetime.year(0.0), datetime.month(0.0), datetime.day(0.0))
io.print(datetime.hour(0.0), datetime.minute(0.0), datetime.second(0.0))
io.print(datetime.weekday(0.0))
)PURR",
                    "True\n"
                    "True\n"
                    "True\n"
                    "True\n"
                    "1970-01-01 00:00:00\n"
                    "1970 1 1\n"
                    "0 0 0\n"
                    "3\n",
                    "TZ=UTC ");
}
