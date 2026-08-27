# `requests` — HTTP for cheatah

An HTTP/1.1 client in the spirit of Python's `requests` — and **the first standard-library
module written in pure cheatah** (`requests.purr`, compiled by `purrc` into an importable
module). All the protocol logic is cheatah source; it rides on the C++-authored stdlib
underneath (`socket` for TCP, `tls` for HTTPS, `parsers` for URL/JSON parsing).

```purr
import io
import requests

let r = requests.get("http://example.com/")
if r.ok() { io.print(r.status_code, r.text()) }

# Verbs, query params, custom headers, timeout, redirect budget.
let o = requests.Options({.timeout_ms = 5000})
o.params["q"] = "cheatah"
let s = requests.get("https://example.com/search", o)
io.print(s.ok(), s.header("content-type"))

# POST a JSON body (to_json serializes a flat dict; pass json_body for anything richer).
let body = requests.Options({.json_body = requests.to_json({"side": "buy"})})
let p = requests.post("https://api.example.com/order", body)
p.raise_for_status()          # raises on 4xx/5xx (a no-op otherwise)

# Form data and Basic auth.
let f = requests.Options({.auth_user = "key", .auth_pass = "secret"})
f.data["symbol"] = "SPX"
let q = requests.post("https://api.example.com/quote", f)   # application/x-www-form-urlencoded
```

## Verbs

`get`, `post`, `put`, `patch`, `delete`, `head`, `options`, plus the generic
`request(method, url, o)`. Each takes an optional `Options`; `head` returns headers only
(empty body). `delete` works because purrc escapes the C++ keyword in codegen.

## Request bodies (`Options`)

One of, in precedence order: `json_body` (a pre-serialized JSON string → `application/json`),
`data` (a `dict<str,str>` → `application/x-www-form-urlencoded`, percent-encoded), or `body`
(a raw string). `Content-Type` and `Content-Length` are added automatically unless you set
them yourself. GET/HEAD never carry a body. `to_json(dict<str,str>)` serializes the common
flat-object case (build the string yourself for nested/non-string JSON).

## Auth

`auth_user`/`auth_pass` add HTTP Basic (`Authorization: Basic <base64>`). For Bearer tokens
or API-key/HMAC schemes, set the `Authorization` (or any) header yourself — a caller-supplied
header is never overridden.

## `Options`

`params`, `headers`, `timeout_ms` (default 30000), `max_redirects` (default 5),
`no_redirect`, `body`, `data`, `json_body`, `auth_user`, `auth_pass`,
`max_bytes` (max response body; default 100 MiB — a hard cap so a hostile/compromised server
cannot exhaust memory), `insecure` (https: skip TLS cert validation; default false = verify),
`ca_file` (https: a PEM CA bundle to trust instead of the system store).

## `Response`

Fields `status_code`, `reason`, `headers`, `body`, `url`, `error`, `cookies` (parsed from
`Set-Cookie`), `history` (intermediate responses when redirects were followed). Methods:
`ok()` (2xx and no error), `header(name)` (case-insensitive), `text()`/`content()` (the body),
`json(out)` (typed parse into a struct via the accelerated `parsers.json.read`),
`raise_for_status()` (raises on 4xx/5xx), `is_redirect()`, `is_permanent_redirect()`.

A non-2xx status is a **completed** exchange: `error` stays empty; only transport failures
(DNS/connect/timeout/TLS/malformed) set `error` and leave `status_code` 0. Redirects
(301/302/303/307/308) are followed by default; 303 (and 301/302 on a POST) continue as GET
with the body dropped, matching Python. Header names are stored lowercased. Internally
`requests` opens the TCP fd and (for https) a `tls.Conn` guard, so a request never leaks its
connection even on an error path.

## Security notes

- <b>`https://` authenticates the server by default.</b> It rides the cheatah `tls` client, which
  validates the certificate chain to a trusted CA, matches the hostname against the certificate
  SAN, and checks expiry (see the [`tls` README](../tls/README.md)) — so an active
  man-in-the-middle is refused, not just a passive eavesdropper. For a pinned/controlled peer,
  set `Options.insecure = true` to skip validation, or `Options.ca_file` to trust a specific PEM
  CA bundle (e.g. a private CA).
- **Response size is capped** at `Options.max_bytes` (default 100 MiB): a server that streams an
  unbounded body — or declares an oversized `Content-Length` — fails with an error instead of
  exhausting memory.
- **Malformed framing never crashes.** A non-numeric/overflowing/negative `Content-Length` or a
  malformed status line, and an overflowing chunk size, set `error`; they do not raise.
- **Cross-host redirects drop credentials.** On a redirect to a different host, Basic-auth
  (`auth_user`/`auth_pass`) and any `Authorization`/`Cookie` header are stripped before the next
  hop, so secrets scoped to the original host are never sent to another origin. (Custom auth
  headers like `X-Api-Key` that you set yourself are your responsibility across hosts.) The
  caller's `Options` is never mutated — the request works on a private copy.

## Deviations from Python `requests` (by design)

- <b>`json()` takes a struct</b> (`r.json(out)`) and uses the accelerated schema-typed reader,
  rather than returning a dynamic object. A struct-free dynamic `json()` for ad-hoc navigation
  is planned once `parsers.json` gains `.purr`-navigable accessors.
- <b>`text` and `content` are identical</b> — cheatah strings are byte-based, so there is no
  separate decoded-text vs bytes distinction.
- <b>Redirect opt-out is `no_redirect` (not `allow_redirects`).</b> cheatah zero-initializes
  structs, so a true-by-default `allow_redirects` bool would silently become `false` on any
  hand-built `Options`. Instead redirects are followed by default (the zero value) and you set
  `.no_redirect = true` to stop at the 3xx — the same behavior as Python's `allow_redirects=False`.
- <b>`params` iterate in unspecified order</b> (`dict<str,str>`). If you must sign an exact query
  string (HMAC), put the query in the URL rather than in `params`.
- **One connection per request** (`Connection: close`); no keep-alive/`Session` yet.

## Not yet supported

`Session`/keep-alive/connection reuse, retries, multipart/`files=`, streaming/`iter_content`,
and proxies.
