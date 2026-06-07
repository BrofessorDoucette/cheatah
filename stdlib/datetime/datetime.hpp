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
 *
 * Reads the wall clock and returns fractional seconds since the Unix epoch
 * (1970-01-01 UTC); the value tracks real time and can jump if the system
 * clock is adjusted (NTP, manual changes).
 * @return seconds since the Unix epoch (`system_clock`).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.NowTodayUtcnowTimestamp
 * @crtest DatetimeCompileRun.Timestamp
 * @systest StdlibE2E.Datetime
 */
double timestamp();
/**
 * Current local time as `"YYYY-MM-DD HH:MM:SS"`.
 *
 * Formats the current epoch in the host's local timezone (subject to DST), so
 * the result differs from `utcnow` by the local UTC offset.
 * @return the formatted string.
 * @complexity O(1) time.
 * @alloc allocates the result string.
 * @test CheatahDatetime.NowTodayUtcnowTimestamp
 * @crtest DatetimeCompileRun.Now
 * @systest StdlibE2E.Datetime
 */
std::string now();
/**
 * Current UTC time as `"YYYY-MM-DDTHH:MM:SSZ"`.
 *
 * Formats the current epoch in UTC (never local time) using ISO 8601 with a
 * `T` separator and trailing `Z`, independent of the host timezone.
 * @return the formatted string.
 * @complexity O(1) time.
 * @alloc allocates the result string.
 * @test CheatahDatetime.NowTodayUtcnowTimestamp
 * @crtest DatetimeCompileRun.Utcnow
 * @systest StdlibE2E.Datetime
 */
std::string utcnow();
/**
 * Current local date as `"YYYY-MM-DD"`.
 *
 * Formats just the calendar date of the current epoch in the host's local
 * timezone; near midnight this may differ by a day from the UTC date.
 * @return the formatted string.
 * @complexity O(1) time.
 * @alloc allocates the result string.
 * @test CheatahDatetime.NowTodayUtcnowTimestamp
 * @crtest DatetimeCompileRun.Today
 * @systest StdlibE2E.Datetime
 */
std::string today();

/**
 * strftime-style formatting of an epoch in local time (e.g. @p fmt = `"%Y-%m-%d"`).
 *
 * Converts @p epoch to local time and expands C `strftime` format codes (`%Y`,
 * `%m`, `%d`, `%H`, etc.); the output is truncated to a 128-byte internal
 * buffer, so very long expansions are cut short.
 * @param epoch epoch seconds.
 * @param fmt a strftime format string.
 * @return the formatted string.
 * @complexity O(1) time.
 * @alloc allocates a temporary `std::string` for @p fmt plus the result.
 * @test CheatahDatetime.Format
 * @crtest DatetimeCompileRun.Format
 * @systest StdlibE2E.Datetime
 */
std::string format(double epoch, std::string_view fmt);

/**
 * Local-time year.
 *
 * Converts @p epoch to the host's local timezone and returns the calendar
 * year; the fractional part of @p epoch is truncated toward the epoch.
 * @param epoch epoch seconds.
 * @return the 4-digit year.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.ComponentsOfKnownEpoch
 * @crtest DatetimeCompileRun.Year
 * @systest StdlibE2E.Datetime
 */
int year(double epoch);
/**
 * Local-time month.
 *
 * Returns the calendar month of @p epoch in the host's local timezone,
 * already shifted to a 1-based value (January is 1, not the C `tm_mon` 0).
 * @param epoch epoch seconds.
 * @return the month, 1..12.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.ComponentsOfKnownEpoch
 * @crtest DatetimeCompileRun.Month
 * @systest StdlibE2E.Datetime
 */
int month(double epoch);
/**
 * Local-time day of month.
 *
 * Returns the day-of-month of @p epoch in the host's local timezone; this is
 * the calendar day, not the day-of-year or weekday.
 * @param epoch epoch seconds.
 * @return the day, 1..31.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.ComponentsOfKnownEpoch
 * @crtest DatetimeCompileRun.Day
 * @systest StdlibE2E.Datetime
 */
int day(double epoch);
/**
 * Local-time hour.
 *
 * Returns the hour of @p epoch in the host's local timezone on a 24-hour
 * clock, so DST transitions can make hours repeat or be skipped.
 * @param epoch epoch seconds.
 * @return the hour, 0..23.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.TimeOfDayComponents
 * @crtest DatetimeCompileRun.Hour
 * @systest StdlibE2E.Datetime
 */
int hour(double epoch);
/**
 * Local-time minute.
 *
 * Returns the minute-within-the-hour of @p epoch in the host's local
 * timezone.
 * @param epoch epoch seconds.
 * @return the minute, 0..59.
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.TimeOfDayComponents
 * @crtest DatetimeCompileRun.Minute
 * @systest StdlibE2E.Datetime
 */
int minute(double epoch);
/**
 * Local-time second.
 *
 * Returns the whole second-within-the-minute of @p epoch (the fractional part
 * of @p epoch is discarded); the value can reach 60 to represent a leap second.
 * @param epoch epoch seconds.
 * @return the second, 0..60 (leap).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.TimeOfDayComponents
 * @crtest DatetimeCompileRun.Second
 * @systest StdlibE2E.Datetime
 */
int second(double epoch);
/**
 * Local-time weekday.
 *
 * Returns the day of the week for @p epoch in the host's local timezone,
 * remapped from the C `tm_wday` (Sunday=0) to Python's Monday=0 convention.
 * @param epoch epoch seconds.
 * @return Monday=0 .. Sunday=6 (Python convention).
 * @complexity O(1) time.
 * @alloc none.
 * @test CheatahDatetime.ComponentsOfKnownEpoch
 * @crtest DatetimeCompileRun.Weekday
 * @systest StdlibE2E.Datetime
 */
int weekday(double epoch);

} // namespace cheatah::datetime
