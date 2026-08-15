# The cheatah runtime {#runtime}

<div class="cheetah-slogan">🐱 <em>A tiny, headless host that loads your module and runs it.</em> 🐆</div>

`purrc` turns a `.purr` into a **native loadable module** — a `.so` (Linux), `.dylib`
(macOS), or `.dll` (Windows). The <b>`cheatah`</b> runtime is the small program that loads and
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

## Flags

Options come **before** the program path; everything after the program forwards to `sys.argv`
verbatim. The full set:

| Flag | Effect |
|---|---|
| `--verify`, `--verify=strict` | Turn on **strict** verification: a valid `.sig` from a trusted key is required. Verification only ever **escalates** — there is no flag to turn it *off*, so a strict deployment can't be silently downgraded from the command line. |
| `--trust <keyfile>`, `--trust=<keyfile>` | Trust list of authorized **code-signing** Ed25519 keys (one 64-hex key per non-comment line). Overrides `CHEATAH_TRUST`. |
| `--trust-runtime <keyfile>`, `--trust-runtime=<keyfile>` | A **separate** trust list for the `.rt` runtime manifest; when set, a valid `.rt.sig` is required too. Overrides `CHEATAH_RT_TRUST`. |
| `--version`, `-v` | Print the runtime version and exit. |
| `--help`, `-h` | Print usage and exit. |

An unknown `-…` option before the program is an error. `sys.argv[0]` is the program path; any
further arguments become `sys.argv[1:]`.

### Environment variables

| Variable | Effect |
|---|---|
| `CHEATAH_VERIFY` | `strict` / `1` / `on` / `yes` / `true` turns on strict verification (same as `--verify`). |
| `CHEATAH_TRUST` | Default path to the code-signing trust list (a leading `--trust` overrides it). |
| `CHEATAH_RT_TRUST` | Default path to the runtime-manifest trust list (a leading `--trust-runtime` overrides it). |

When a trust variable is unset, the runtime falls back to a default file under the config dir
(`$XDG_CONFIG_HOME/cheatah/` or `~/.config/cheatah/`): `trusted.pub` for code signing and
`trusted-runtime.pub` for the runtime manifest.

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
