#!/usr/bin/env python3
"""HONEST cheatah `requests` vs Python `requests` comparison.

Both clients hit the SAME local loopback HTTP server (a threaded http.server in this
process), each runs the SAME request N times, and the body length is accumulated and
printed so nothing can be optimized away (and to prove both sides did real work). We do
NOT tilt it: cheatah's `requests` opens ONE connection per request (`Connection: close`),
so the fair Python peer is the top-level `requests.get`/`.post` API — which likewise
creates a fresh connection per call (its functional API opens a new Session each time),
not a pooled `Session`. When cheatah gains keep-alive Sessions, add a pooled column.

The cheatah side compiles a real `.purr` that loops the request and times it with
`time.monotonic()` inside the process (so process startup is excluded); the Python side
loops in CPython with `time.monotonic()`. The two clients are STRIATED — each of ROUNDS
rounds runs cheatah and then Python before either repeats — and the reported speedup is the
median of the per-round PAIRED ratios, printed with the full range of those ratios. That
band matters more here than anywhere else in the tree: this harness talks to a loopback
server through the kernel, so its run-to-run spread is genuinely wide and a single number
would hide it. Build `release` first:

    python3 scripts/requests_compare.py
"""
import atexit
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PURRC = os.path.join(ROOT, "build", "release", "bin", "purrc")
CHEATAH = os.path.join(ROOT, "build", "release", "bin", "cheatah")
# 7 striated rounds replacing 3 best-of-N trials per side — see scripts/perf_suite.py.
ROUNDS = 7
GET_N = 300
POST_N = 300
BODY = b'{"symbol":"SPX","price":7386.65,"live":true}'


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *a):  # silence
        pass

    def _reply(self):
        self.send_response(200)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(BODY)))
        self.end_headers()
        self.wfile.write(BODY)

    def do_GET(self):
        self._reply()

    def do_POST(self):
        n = int(self.headers.get("Content-Length", "0"))
        if n:
            self.rfile.read(n)
        self._reply()


def start_server():
    srv = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return srv, srv.server_address[1]


def run(argv):
    return subprocess.run(argv, capture_output=True, text=True)


_TEMPDIRS = []
atexit.register(lambda: [shutil.rmtree(d, ignore_errors=True) for d in _TEMPDIRS])


def build_cheatah(port, verb, n):
    """Compile + run a .purr that loops `verb` n times against the server, timing inside."""
    url = f"http://127.0.0.1:{port}/bench"
    if verb == "get":
        call = f'let resp = requests.get("{url}")'
    else:
        call = (
            'let o = requests.Options({.json_body = "' + BODY.decode().replace('"', '\\"') + '"})\n'
            f'    let resp = requests.post("{url}", o)'
        )
    src = (
        "import io\nimport time\nimport requests\n"
        "let acc = 0\n"
        "let t0 = time.monotonic()\n"
        f"for i in range(0, {n}) {{\n    {call}\n    acc = acc + len(resp.body)\n}}\n"
        "let dt = time.monotonic() - t0\n"
        "io.print(dt)\nio.print(acc)\n"
    )
    # ONE timed pass per invocation, and compiled once: bench() below alternates this with
    # the Python client round by round. Network-shaped work drifts more than compute does
    # (connection setup, kernel buffers, the server's own scheduling), so measuring all of
    # one client's passes and then all of the other's is especially misleading here.
    d = tempfile.mkdtemp(prefix="requests_compare.")
    _TEMPDIRS.append(d)
    purr, so = os.path.join(d, "b.purr"), os.path.join(d, "b.so")
    open(purr, "w").write(src)
    c = run([PURRC, purr, "-o", so])
    if c.returncode != 0:
        sys.exit("cheatah compile failed:\n" + c.stderr + c.stdout)
    return so


def ch_once(so):
    r = run([CHEATAH, so])
    if r.returncode != 0:
        sys.exit("cheatah run failed:\n" + r.stderr + r.stdout)
    lines = [l for l in r.stdout.splitlines() if l.strip()]
    return float(lines[0]), int(lines[1])


def _median(xs):
    ys = sorted(xs)
    n = len(ys)
    return ys[n // 2] if n % 2 else 0.5 * (ys[n // 2 - 1] + ys[n // 2])


def py_once(port, verb, n):
    try:
        import requests as rq
    except Exception:
        return None, None
    url = f"http://127.0.0.1:{port}/bench"
    acc = 0
    t0 = time.monotonic()
    for _ in range(n):
        resp = rq.get(url) if verb == "get" else rq.post(url, data=BODY)
        acc += len(resp.content)
    return time.monotonic() - t0, acc


def bench(label, port, verb, n):
    so = build_cheatah(port, verb, n)
    ch_ts, py_ts, ratios = [], [], []
    have_py = True
    for _ in range(ROUNDS):
        ct, _ = ch_once(so)                     # cheatah and python adjacent within a round
        pt, _ = py_once(port, verb, n)
        ch_ts.append(ct)
        if pt is None:
            have_py = False
            break
        py_ts.append(pt)
        ratios.append(pt / ct)

    cu = _median(ch_ts) / n * 1e6               # µs/request
    row = f"{label:<10} {n:>6}  cheatah {cu:8.1f} µs/req"
    if not have_py:
        print(row + "   (python `requests` not installed — cheatah-only)")
        return
    pu = _median(py_ts) / n * 1e6
    ratio = _median(ratios)                     # median of PAIRED ratios
    lo, hi = min(ratios), max(ratios)
    winner = f"cheatah {ratio:.2f}× faster" if ratio > 1 else f"python {1 / ratio:.2f}× faster"
    # Dispersion is printed prominently, not tucked away: this is the one harness whose
    # numbers depend on a loopback server and a kernel, so a wide band is the finding.
    print(f"{row}   python {pu:8.1f} µs/req   → {winner}   "
          f"[per-round ratios {lo:.2f}–{hi:.2f}]")


def main():
    for b in (PURRC, CHEATAH):
        if not os.path.exists(b):
            sys.exit(f"missing {b} — build the `release` preset first "
                     "(cmake --build --preset release)")
    srv, port = start_server()
    print(f"local loopback server on 127.0.0.1:{port} — one connection per request, "
          f"{ROUNDS} striated rounds (cheatah and python alternate within each round)\n")
    bench("GET", port, "get", GET_N)
    bench("POST+JSON", port, "post", POST_N)
    srv.shutdown()


if __name__ == "__main__":
    main()
