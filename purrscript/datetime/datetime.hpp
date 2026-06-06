#pragma once

// purrscript datetime — practical date/time helpers over epoch seconds, mirroring
// the useful core of https://docs.python.org/3/library/datetime.html. Times are
// represented as epoch seconds (double, from `time`/`timestamp`); formatting and
// component extraction use the C library calendar.
#include <string>
#include <string_view>

namespace cheatah::purrscript::datetime {

double timestamp();                  // current time, epoch seconds
std::string now();                   // local  "YYYY-MM-DD HH:MM:SS"
std::string utcnow();                // UTC    "YYYY-MM-DDTHH:MM:SSZ"
std::string today();                 // local  "YYYY-MM-DD"

// strftime-style formatting of an epoch (local time), e.g. fmt = "%Y-%m-%d".
std::string format(double epoch, std::string_view fmt);

// Local-time components of an epoch.
int year(double epoch);
int month(double epoch);    // 1..12
int day(double epoch);      // 1..31
int hour(double epoch);     // 0..23
int minute(double epoch);   // 0..59
int second(double epoch);   // 0..60
int weekday(double epoch);  // Monday=0 .. Sunday=6 (Python convention)

} // namespace cheatah::purrscript::datetime
