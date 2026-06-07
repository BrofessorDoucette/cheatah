#pragma once

/**
 * @file datetime.hpp
 * @brief cheatah `datetime` — practical date/time helpers over epoch seconds,
 *        mirroring the useful core of Python's `datetime` module. Times are
 *        epoch seconds (`double`, from `time`/`timestamp`); formatting and
 *        component extraction use the C library calendar (local time, except
 *        `utcnow`). `import datetime` to use it.
 *
 * Unit tests: `stdlib/tests/datetime_test.cpp`; the suite runs under
 * AddressSanitizer (the `asan` preset) and Valgrind
 * (`security/run-valgrind.sh`) on every QA-gate run.
 *
 * Doc convention (see also the other stdlib headers): each function documents
 * its runtime complexity with @complexity, its heap allocation with @alloc, and
 * the @test that covers it.
 */
#include <string>
#include <string_view>

namespace cheatah::datetime {

/**
 * Current time.
 * @return seconds since the Unix epoch (`system_clock`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.NowTodayUtcnowTimestamp
 */
double timestamp();
/**
 * Current local time as `"YYYY-MM-DD HH:MM:SS"`.
 * @return the formatted string.
 * @complexity O(1) time.
 * @alloc allocates the result string.
 * @test CheatahDatetime.NowTodayUtcnowTimestamp
 */
std::string now();
/**
 * Current UTC time as `"YYYY-MM-DDTHH:MM:SSZ"`.
 * @return the formatted string.
 * @complexity O(1) time.
 * @alloc allocates the result string.
 * @test CheatahDatetime.NowTodayUtcnowTimestamp
 */
std::string utcnow();
/**
 * Current local date as `"YYYY-MM-DD"`.
 * @return the formatted string.
 * @complexity O(1) time.
 * @alloc allocates the result string.
 * @test CheatahDatetime.NowTodayUtcnowTimestamp
 */
std::string today();

/**
 * strftime-style formatting of an epoch in local time (e.g. @p fmt = `"%Y-%m-%d"`).
 * @param epoch epoch seconds.
 * @param fmt a strftime format string.
 * @return the formatted string.
 * @complexity O(1) time.
 * @alloc allocates a temporary `std::string` for @p fmt plus the result.
 * @test CheatahDatetime.Format
 */
std::string format(double epoch, std::string_view fmt);

/**
 * Local-time year.
 * @param epoch epoch seconds.
 * @return the 4-digit year.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.ComponentsOfKnownEpoch
 */
int year(double epoch);
/**
 * Local-time month.
 * @param epoch epoch seconds.
 * @return the month, 1..12.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.ComponentsOfKnownEpoch
 */
int month(double epoch);
/**
 * Local-time day of month.
 * @param epoch epoch seconds.
 * @return the day, 1..31.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.ComponentsOfKnownEpoch
 */
int day(double epoch);
/**
 * Local-time hour.
 * @param epoch epoch seconds.
 * @return the hour, 0..23.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.TimeOfDayComponents
 */
int hour(double epoch);
/**
 * Local-time minute.
 * @param epoch epoch seconds.
 * @return the minute, 0..59.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.TimeOfDayComponents
 */
int minute(double epoch);
/**
 * Local-time second.
 * @param epoch epoch seconds.
 * @return the second, 0..60 (leap).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.TimeOfDayComponents
 */
int second(double epoch);
/**
 * Local-time weekday.
 * @param epoch epoch seconds.
 * @return Monday=0 .. Sunday=6 (Python convention).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.ComponentsOfKnownEpoch
 */
int weekday(double epoch);

} // namespace cheatah::datetime
