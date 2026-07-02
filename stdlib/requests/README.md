# `requests` — HTTP for cheatah

An HTTP/1.1 client in the spirit of Python's `requests` — and **the first standard-library
module written in pure cheatah** (`requests.purr`, compiled by `purrc` into an importable
module). All the protocol logic is cheatah source; it rides on the C++-authored stdlib
underneath (`socket` for TCP, `tls` for HTTPS, `parsers.url` for URL parsing).

```python
import io
import requests

let r = requests.get("http://example.com/")
if r.ok() { io.print(r.status, r.body) }

# Options: query params, custom headers, timeout, redirect budget.
let o = Options({.timeout_ms = 5000})
o.params["q"] = "cheatah"
let q = requests.get("https://example.com/search", o)
io.print(q.ok(), q.header("content-type"))
```

## What it covers

- `http://` and `https://` (via the from-scratch cheatah `tls` client — the server is
  authenticated or the request fails).
- Query parameters (percent-encoded), custom headers (a default `User-Agent` unless you set
  one), Content-Length / chunked / close-delimited body framing, redirect following (per a
  `max_redirects` budget), and per-request timeouts.
- Case-insensitive response headers (`r.header("Content-Type")`).

## Types

- **`Options`** — `params`, `headers`, `timeout_ms` (default 30000), `max_redirects` (default 5).
- **`Response`** — `status`, `headers`, `body`, `url`, `error`, plus `ok()` (2xx and no error)
  and `header(name)` (case-insensitive lookup). A non-2xx status is a **completed** exchange:
  `error` stays empty; transport failures (DNS/connect/timeout/TLS/malformed) set `error` and
  leave `status` 0.

Internally, `requests` opens the TCP fd and (for https) a `tls.Conn` guard, so a request never
leaks its connection even on an error path.
