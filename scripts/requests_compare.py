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
loops in CPython with `time.monotonic()`. Best-of-TRIALS each. Build `release` first:

    python3 scripts/requests_compare.py
"""
import os
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PURRC = os.path.join(ROOT, "build", "release", "bin", "purrc")
CHEATAH = os.path.join(ROOT, "build", "release", "bin", "cheatah")
TRIALS = 3
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


def time_cheatah(port, verb, n):
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
        "let best = 1000000.0\n"
        f"for t in range(0, {TRIALS}) {{\n"
        "    let t0 = time.monotonic()\n"
        f"    for i in range(0, {n}) {{\n        {call}\n        acc = acc + len(resp.body)\n    }}\n"
        "    let dt = time.monotonic() - t0\n"
        "    if dt < best { best = dt }\n"
        "}\n"
        "io.print(best)\nio.print(acc)\n"
    )
    with tempfile.TemporaryDirectory() as d:
        purr, so = os.path.join(d, "b.purr"), os.path.join(d, "b.so")
        open(purr, "w").write(src)
        c = run([PURRC, purr, "-o", so])
        if c.returncode != 0:
            sys.exit("cheatah compile failed:\n" + c.stderr + c.stdout)
        r = run([CHEATAH, so])
        if r.returncode != 0:
            sys.exit("cheatah run failed:\n" + r.stderr + r.stdout)
        lines = [l for l in r.stdout.splitlines() if l.strip()]
        return float(lines[0]), int(lines[1])


def time_python(port, verb, n):
    try:
        import requests as rq
    except Exception:
        return None, None
    url = f"http://127.0.0.1:{port}/bench"
    best, acc = float("inf"), 0
    for _ in range(TRIALS):
        acc = 0
        t0 = time.monotonic()
        for _ in range(n):
            resp = rq.get(url) if verb == "get" else rq.post(url, data=BODY)
            acc += len(resp.content)
        dt = time.monotonic() - t0
        best = min(best, dt)
    return best, acc


def bench(label, port, verb, n):
    ct, cacc = time_cheatah(port, verb, n)
    pt, pacc = time_python(port, verb, n)
    cu = ct / n * 1e6  # µs/request
    row = f"{label:<10} {n:>6}  cheatah {cu:8.1f} µs/req"
    if pt is None:
        print(row + "   (python `requests` not installed — cheatah-only)")
        return
    pu = pt / n * 1e6
    ratio = pt / ct
    winner = f"cheatah {ratio:.2f}× faster" if ratio > 1 else f"python {1/ratio:.2f}× faster"
    print(f"{row}   python {pu:8.1f} µs/req   → {winner}")


def main():
    for b in (PURRC, CHEATAH):
        if not os.path.exists(b):
            sys.exit(f"missing {b} — build the `release` preset first "
                     "(cmake --build --preset release)")
    srv, port = start_server()
    print(f"local loopback server on 127.0.0.1:{port} — one connection per request, "
          f"best of {TRIALS} trials\n")
    bench("GET", port, "get", GET_N)
    bench("POST+JSON", port, "post", POST_N)
    srv.shutdown()


if __name__ == "__main__":
    main()
