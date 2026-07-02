# The cheatah runtime {#runtime}

<div class="cheetah-slogan">🐱 <em>A tiny, headless host that loads your module and runs it.</em> 🐆</div>

`purrc` turns a `.purr` into a **native loadable module** — a `.so` (Linux), `.dylib`
(macOS), or `.dll` (Windows). The **`cheatah`** runtime is the small program that loads and
runs it:

```sh
cheatah hello.so            # run a module
cheatah app.so a b c        # extra args are forwarded as sys.argv (sys.argv[0] = "app.so")
```

There is no interpreter and no VM. The runtime `dlopen`s the module, resolves the one
exported entry point (`purr_main`, emitted by purrc — there is no `main()`), and calls it.
That's the whole model: your code is machine code, and the host just hands control to it.

## What it does before it runs your code

A compiled module is native code, so loading it *is* running it. The runtime does not blindly
load — before `dlopen` it **validates** the file (canonicalizes the path, requires a regular
file, **refuses a world-writable** module, and checks the ELF/Mach-O magic), then applies any
**integrity** checks that are present:

| Sidecar (next to `app.so`) | Checked | Guards against |
|---|---|---|
| `app.so.sha512` | **always** (auto) | accidental corruption |
| `app.so.rt` | **always** (auto) | a module built for a *newer* C runtime than this host |
| `app.so.sig` | only under `--verify` / `CHEATAH_VERIFY=strict` | tampering — an Ed25519 signature from a **trusted key** |
| `app.so.rt.sig` | when a separate runtime trust is set | tampering of the runtime manifest |

```sh
cheatah app.so                                  # auto-checks the .sha512 and .rt, then runs
CHEATAH_VERIFY=strict cheatah --trust rel.pub app.so   # ALSO require a valid .sig from rel.pub
```

Trust is chosen by the host, not the module: `--trust <keyfile>` / `CHEATAH_TRUST` pins the
code-signing public key(s); `--trust-runtime` / `CHEATAH_RT_TRUST` pins a **separate** runtime
key. Under strict verification a module without a valid signature from a trusted key is
**refused, not run**.

## The trust model

cheatah is **single-trust by default**: a module runs with **your full privileges**, exactly
like a Python script or a compiled C++ program — there is **no sandbox** yet. Signing proves a
module is *authentically from a signer and unmodified*; it does **not** prove it is safe. Run
only code you wrote, audited, or that a key you trust has signed. The full threat model and the
sandbox/MCP roadmap live in [Security](security.html).

## See also

- [purrc](purrc.html) — how a `.purr` becomes the `.so` this runtime loads, and how to sign it.
- [Imports & module resolution](imports.html) — how a program finds the stdlib and extension
  modules it imports.
- [biome](biome.html) — the package manager that builds and wires it all together.
