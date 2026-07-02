// A real WebSocket echo server behind TLS, built ONLY on Node's `ws` library and
// Node's built-in `https`/`tls`. cheatah's from-scratch websocket client is tested
// against THIS reference implementation (the `ws` library owns RFC 6455; Node owns
// TLS). Usage: node wss_echo_server.js <cert.pem> <key.pem> <port>
//
// Behavior: echoes every message back verbatim (text -> text, binary -> binary),
// answers ping with pong (the `ws` library does this automatically), and closes
// cleanly when the client sends a close frame. Prints "READY" once bound so the
// test harness can wait for the listen instead of racing it.
//
// The 4th argv selects a MODE that drives one of the cheatah client's refusal/edge
// paths against a REAL peer (still only Node's built-in tls/https + the ws library —
// no protocol bytes are hand-mirrored except where the ws library itself frames them):
//   (default) : ws echo server (round trips + control-frame commands below).
//   plain     : plain HTTPS, answers 200 (never 101) -> "did not switch protocols".
//   drop      : a Node tls server that completes the TLS handshake then destroys the
//               socket with NO HTTP response -> "connection closed during upgrade".
//   flood     : a Node tls server that, after TLS, writes >64 KiB with no blank line
//               -> "upgrade response too large / not a WebSocket server".
// (A raw TCP peer for the TLS-handshake-failure path is set up in-process by the test.)
'use strict';
const fs = require('fs');
const https = require('https');
const tls = require('tls');

const [certPath, keyPath, portArg, mode] = process.argv.slice(2);
const port = parseInt(portArg, 10);
const creds = { cert: fs.readFileSync(certPath), key: fs.readFileSync(keyPath) };

function ready() {
  process.stdout.write('READY\n');
}

if (mode === 'drop' || mode === 'flood') {
  // A real TLS 1.3 peer (Node's built-in tls) that completes the handshake but never
  // sends a valid WebSocket upgrade response — exercises the upgrade error paths.
  const tsrv = tls.createServer(creds, (sock) => {
    sock.on('data', () => {
      if (mode === 'drop') {
        sock.destroy();  // close with no response -> "closed during upgrade"
      } else {
        // > 64 KiB of header bytes with no terminating blank line.
        sock.write('HTTP/1.1 101 x\r\n' + 'X-Pad: ' + 'A'.repeat(70000) + '\r\n');
      }
    });
  });
  tsrv.listen(port, '127.0.0.1', ready);
  return;
}

const server = https.createServer(
  creds,
  (req, res) => {
    // Only reached in "plain" mode (no upgrade listener): answer 200, never 101.
    res.writeHead(200, { 'Content-Type': 'text/plain' });
    res.end('not a websocket server\n');
  }
);

if (mode !== 'plain') {
  const { WebSocketServer } = require('ws');
  const wss = new WebSocketServer({ server });
  wss.on('connection', (ws) => {
    ws.on('message', (data, isBinary) => {
      const text = isBinary ? null : data.toString();
      // A handful of text control commands drive the harder-to-reach client paths
      // against this REAL ws peer. Everything else is echoed verbatim (opcode-preserving).
      if (text === 'close') {
        // Clean RFC 6455 close handshake (server-initiated close frame).
        ws.close(1000, 'bye');
        return;
      }
      if (text === 'ping') {
        // Server-initiated ping (client's recv must answer pong, then keep reading);
        // follow with a pong (client ignores it), then a real message it returns.
        ws.ping(Buffer.from('pong-me'));
        ws.pong(Buffer.from('ignored'));
        ws.send('after-ping');
        return;
      }
      if (text === 'frag') {
        // A message split into two frames using ws's OWN sender: text (fin=false) +
        // continuation (fin=true). Exercises the client's reassembly/continuation path.
        ws._sender.send(Buffer.from('frag-one|'), { fin: false, opcode: 0x01, mask: false, rsv1: false });
        ws._sender.send(Buffer.from('frag-two'), { fin: true, opcode: 0x00, mask: false, rsv1: false });
        return;
      }
      if (text === 'masked') {
        // A MASKED server->client frame. Servers MUST NOT mask (RFC 6455 §5.1), but the
        // cheatah client unmasks defensively; ask ws's OWN sender to mask so that path is
        // exercised without hand-writing any frame bytes in our tree.
        ws._sender.send(Buffer.from('masked-ok'), { fin: true, opcode: 0x01, mask: true, rsv1: false });
        return;
      }
      ws.send(data, { binary: isBinary });
    });
  });
}

server.listen(port, '127.0.0.1', ready);
