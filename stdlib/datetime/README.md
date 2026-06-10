# cheatah `datetime`

Practical date/time helpers over epoch seconds. Times are epoch seconds (`double`,
from `time`/`timestamp`); formatting and component extraction use the C library
calendar (local time, except `utcnow`).

```purr
import datetime

stamp = datetime.timestamp()
print(datetime.now())                    # "YYYY-MM-DD HH:MM:SS"
print(datetime.format(stamp, "%Y-%m-%d"))
print(datetime.year(stamp), datetime.weekday(stamp))
```

## Functions

Current time / formatted strings:

- `timestamp()` — current time as epoch seconds.
- `now()` — current local time, `"YYYY-MM-DD HH:MM:SS"`.
- `utcnow()` — current UTC time, `"YYYY-MM-DDTHH:MM:SSZ"`.
- `today()` — current local date, `"YYYY-MM-DD"`.
- `format(epoch, fmt)` — strftime-style formatting of an epoch (local time).

Local-time components of an epoch:

- `year(epoch)` / `month(epoch)` / `day(epoch)` — calendar date parts.
- `hour(epoch)` / `minute(epoch)` / `second(epoch)` — time-of-day parts.
- `weekday(epoch)` — Monday=0 .. Sunday=6 (Python convention).

Per-function docs (parameters, runtime complexity, heap behavior) are in
[datetime.hpp](datetime.hpp). Tested in
[../tests/datetime_test.cpp](../tests/datetime_test.cpp); ASan + Valgrind clean
via the QA gate (`security/run-valgrind.sh`).
