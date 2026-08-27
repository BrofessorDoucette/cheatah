# cheatah `websocket` 🐆

A from-scratch, low-latency **WebSocket client** (RFC 6455) over the cheatah
`tls` 1.3 client and `socket`. **wss:// by default**; plaintext `ws://` is accepted only
to a loopback host (a local control plane such as a DevTools port). No external libraries.

```purr
import io
import websocket

with websocket.open("echo.websocket.org", 443, "/", "echo.websocket.org") as ws {
    ws.send_text("hello")
    io.print(ws.recv())          # the echoed message
}                                # the connection (frame + TLS + socket) closes here
```

`open_url("wss://host[:port]/path")` is the convenience form. Both return an owning
`Client` guard whose destructor sends a close frame and tears down the TLS session and
socket — so, used with `with`, a connection cannot leak on any exit path.

## Built for speed

The receive path is the hot path and is allocation-quiet:

- **one read buffer per session, reused** across every frame — no per-frame heap;
- **server→client frames are unmasked** by the protocol (§5.1), so `recv` does
  **zero** unmasking work — it slices the payload straight out of the buffer;
- **frame headers are parsed in place** (no header object is built);
- the TLS layer is **drained in 64 KiB chunks**, so many frames decode per read.

The send path masks (clients MUST, §5.3) with a **64-bit-word XOR** (8 bytes per
step) and a CSPRNG key from `os.urandom`; sends are rare (subscribe/control), off
the hot path. Control frames are handled transparently — a ping is answered with
a pong, a close ends the stream — none of which the caller sees.

## API

The cheatah-facing API is the owning <b>`Client`</b> guard from `open`/`open_url`:

| Call | What |
|------|------|
| `open(host, port, path, server_name)` | TCP + TLS 1.3 + the RFC 6455 upgrade; returns a `Client` guard |
| `open_url(url)` | same, from a `wss://…` URL |
| `ws.send_text(msg)` | send one masked text frame |
| `ws.recv()` | the next application message (reassembled; control frames handled); `""` on close |
| `ws.close()` | send a close frame and tear down (the destructor does this too) |

The flat, session-id handle API (`connect`/`connect_url`/`send_text`/`recv`/`close`,
keyed by an integer that must be closed by hand to free its heap `Session`) is
**C++-only** — it lives in `websocket_lowlevel.hpp` and is **not reachable from
cheatah**, so a cheatah program cannot leak the session.

A client is single-owner — don't `recv` and `send_text` the same client from two
threads at once; separate clients are independent.

## Security

`recv()` treats every server frame as hostile: it enforces RFC 6455 framing before trusting a
length — **per-frame (64 MiB) and reassembled-message (64 MiB) size caps** (so a huge or
`header + len`-overflowing length cannot drive an out-of-bounds unmask or exhaust memory),
**control frames ≤125 bytes and never fragmented**, reserved-bit / undefined-opcode rejection,
and a validated fragmentation state machine. A malformed or oversized frame fails the
connection instead of corrupting memory.

`wss://` **authenticates the server by default**: the underlying `tls` client validates the
certificate chain to a trusted CA, matches the hostname, and checks expiry — so a `wss://`
connection resists an active MITM. For a pinned/controlled peer, `open`/`open_url` take an
`insecure` flag (skip validation) and a `ca_file` (trust a specific PEM CA) — see the
[`tls` README](../tls/README.md).

Built on [`tls`](../tls/) (TLS 1.3 with Ed25519, ECDSA P-256/P-384 and RSA server
certificates, chain-validated), [`socket`](../socket/), and [`os`](../os/) (`urandom`).
