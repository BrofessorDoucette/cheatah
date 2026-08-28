# Documentation contract audit — withheld findings, adjudicated

2026-08-27. Not a rendered page: this is the audit's own record, kept in the repo because
the working artifacts lived in a session scratchpad under `/tmp`.

The audit checked 1,005 documented claims against the code, the tests and the generated
artifacts. 385 fixes were applied directly. Every finding also went through an adversarial
re-check, and **54 drew a dissent** — one reviewer arguing the finding was wrong. Those were
reported rather than applied, which is the right default for an automated pass but leaves the
claims undecided. This file decides them.

| outcome | count |
|---|--:|
| rejected — the dissent was right | 49 |
| already applied by a neighbouring accepted fix | 3 |
| code defects, fixed this session | 2 |
| **total** | **54** |

A high rejection rate is the expected shape. These 54 are precisely the contested tail; the
385 uncontested findings were applied. Nothing here was rubber-stamped — the reasoning below
is the dissent's, checked against the current source, and several were re-verified by hand.

## Code defects — resolved

- **A1-005** — `linalg::matmul` out-form read `b.shape()[2]` out of bounds. Fixed in `88a6e8e`.
- **M5-026** — the TSan lane ran a stale `cheatah_memory_tests` binary. Fixed in `1507b13`.

## Already applied

Three findings were fixed as a side effect of an accepted finding rewriting the same region,
so the proposed text is already in the source:

- **G7-001** — `compiler/PYTHON.md`: | Exceptions | `try { … } except e { … }`, `raise "msg"` | `try/except`, `raise` |
- **A12-007** — `stdlib/memory/request.hpp`: Omit it to rely purely on the lease's `valid()`/`expired()` polling. One-shot.
- **A10-026** — `stdlib/os/os.hpp`: Reads @p n bytes from the operating system's CSPRNG — `getentropy`/`/dev/urandom` on POSIX

## The `@test` contract — 22 findings, one question

Every `test-link` finding turned on the same question: may `@test` name a test that reaches
the function indirectly? The contract at [`docs/mainpage.md:26`](../mainpage.md) says the tag is
*"a link to the unit test that **exercises** the function"* — exercises, not directly calls. A
destructor that runs at scope exit inside the named test, or a member reached through a free
function, is exercised by it. On that reading the dissent was right in all 22 cases.

Three were re-verified by hand rather than taken on the contract alone:

- `A3-021` — `ndarray_sys_test.cpp:58` calls `ndarray.size_of(m)`; `size_of` is one
  unconditional hop to `a.size()`.
- `A8-004` — `CheatahSocket.ConnGuardClosesOnScopeExit` constructs a `sk::Conn` destroyed at
  scope exit, which is the destructor under test.
- `A13-006` — `RegexE2E.FullMatchIsAnchoredBothEnds` exists at `regex_e2e_test.cpp:33`.

`scripts/doc_lint.sh tests` now proves the *existence* half mechanically on every gate run, so
only reachability is left to judgment.

## The substantive findings

Each was re-located in the current source (this session's edits moved several files, so the
recorded line numbers were stale), the implementation read, and the dissent checked.

| id | kind | where | why the finding does not stand |
|---|---|---|---|
| G3-010 | behavior | `docs/biome.md`:280 | cheatah-plot's manifest lists cheatah-gpu as a DIRECT dependency alongside cheatah-gpu-linalg, so 'cheatah-plot builds on cheatah-gpu' is accurate, and standard 0.6.3-alpha does carry both at tested-t… |
| G10-027 | behavior | `stdlib/README.md`:71 | The sentence states what purrc does — static linking of the imported stdlib — and that is exactly what happens; 'for production builds' is an unnecessary qualifier, not a false statement. The explicit… |
| A15-012 | api-surface | `stdlib/aead/aead.hpp`:65<br>`cheatah::aead::chacha20poly1305_encrypt_into` | The finding's own note concedes the substance is right, and I verified it: encrypt_into touches only caller memory plus stack, no globals, no allocation, no errno. The statement is TRUE. The defect is… |
| A15-013 | api-surface | `stdlib/aead/aead.hpp`:93<br>`cheatah::aead::chacha20poly1305_decrypt_into` | Same as A15-012: decrypt_into reads only caller buffers, computes the tag into a stack array, compares in constant time and secure_wipes the stack key — no shared state, no allocation. The claim is TR… |
| A15-014 | api-surface | `stdlib/aead/aead.hpp`:203<br>`cheatah::aead::set_force_portable_crypto` | The finding itself says both values are correct, and aead.cpp:495 confirms a single bool store. O(1) and no allocation are TRUE. This is a one-tag-per-line formatting repair, not a falsity; the replac… |
| A15-015 | api-surface | `stdlib/aead/aead.hpp`:218<br>`cheatah::aead::crypto_hardware_active` | Verified TRUE, and the finding says so ('Formatting only — the values are right'). crypto_hardware_active() is aes_gcm_use_hw() = accel::available() && !g_force_portable; available() memoises in a fun… |
| A6-002 | brief | `stdlib/fixarray/fixarray.hpp`:1099<br>`fixarray::max(const Fixed<T,Dims...>&, T)` | Unlike min, this brief points the RIGHT way: Fixarray.MinMaxClamp pins max({1,5,9},4) == {4,5,9}, i.e. 1 is raised to 4 — exactly 'each element raised to the scalar s', and the formula `max(xi, s)` si… |
| M1-016 | api-surface | `stdlib/linalg/README.md`:105<br>`cheatah::linalg::simd_features` | Refuted. The bullet describes what simd_features returns and offers an illustrative "e.g." — the same example simd.hpp's own @return line uses. No test pins the exact string (LinalgSmoke only asserts … |
| A1-009 | complexity | `stdlib/linalg/backend.hpp`:124<br>`linalg::dot(T&, const Array<T>&, const Array<T>&)` | Nothing in this block is false: the front does validate lengths, and dot_reduce re-checks. The block sits inside `/// @cond INTERNAL` (backend.hpp 113-157), so Doxygen never emits it and doc_tag_lint'… |
| A1-010 | complexity | `stdlib/linalg/backend.hpp`:134<br>`linalg::vdot(T&, const Array<T>&, const Array<T>&)` | Same @cond INTERNAL block as A1-009; no documented statement here is false. vdot routes to the same dot_reduce, so the proposed O(n) / pack-once text is accurate, but its absence is an omission inside… |
| A1-011 | complexity | `stdlib/linalg/backend.hpp`:144<br>`linalg::inner(T&, const Array<T>&, const Array<T>&)` | Same @cond INTERNAL block. inner() is a second name for the Conj::None reduction, so the documented text is accurate as far as it goes; the missing tags are an omission in an excluded region. The prop… |
| A1-012 | complexity | `stdlib/linalg/backend.hpp`:153<br>`linalg::trace(T&, const Array<T>&)` | Same @cond INTERNAL block; the brief ('strided diagonal read, no copy') matches the body exactly, so nothing documented is false. The proposed O(min(r,c)) / @alloc none is correct — trace walks base[o… |
| A1-028 | brief | `stdlib/linalg/routines.hpp`:267<br>`linalg::det(T&, const Array<T>&)` | There is no documented statement here to be true or false — lines 267-269 carry no comment at all, and they sit inside the `/// @cond INTERNAL` region that closes at line 279, so Doxygen excludes them… |
| A1-029 | brief | `stdlib/linalg/routines.hpp`:276<br>`linalg::norm(T&, const Array<T>&)` | Same as A1-028: no comment exists on lines 276-278, inside the @cond INTERNAL region — an omission, not a false claim, and not gated. The inserted content checks out: the contiguous branch reads strai… |
| A1-030 | brief | `stdlib/linalg/routines.hpp`:684<br>`linalg::slogdet(SLogDet&, const Array<T>&)` | The line quoted is the @cond INTERNAL marker itself; the kernel declaration below it is deliberately hidden from the docs, so a missing tag set is not a contract breach. The inserted block is factuall… |
| A2-002 | complexity | `stdlib/linalg/routines.hpp`:411<br>`linalg::svdvals` | The Big-O half is right and the finding concedes it. 'A large constant factor' carries no number, so no table can falsify it — the measured 1.2x-1.9x gap is a factor of roughly two, which many numeric… |
| A2-007 | behavior | `stdlib/linalg/routines.hpp`:465<br>`linalg::eig` | eig's symmetric branch calls symmetric_eig — which is exactly the kernel eigh's real path calls (routines.cpp:1494) — so it does hand the work to eigh's solver and returns eigh's answer promoted to co… |
| A2-010 | param-return | `stdlib/linalg/routines.hpp`:754<br>`linalg::lstsq` | '@param b right-hand side.' is true, just terse — and the same block's detail paragraph already warns that @p b must be conformable for the @ref matmul step, which is precisely the 2-D requirement the… |
| A11-006 | complexity | `stdlib/math/math.hpp`:70<br>`math::min` | This block's own brief scopes it to the family: 'Smallest of two-or-more values (variadic; the overloads chain to fold extra args)'. O(n) in the argument count is the correct bound for that family and… |
| A11-007 | complexity | `stdlib/math/math.hpp`:100<br>`math::max` | Symmetric to A11-006: the brief reads 'Largest of two-or-more values (variadic; the overloads chain to fold extra args)', so O(n) in the argument count is the family's bound and is not false at n == 2… |
| M5-039 | numeric | `stdlib/memory/tests/README.md`:93 | Refuted as stated. Everything else in the bullet checks out (kIters 300 vs 20,000, RUNNING_ON_VALGRIND, stderr announcement, the GTEST_SKIP with reason), and nothing in the repo either establishes or … |
| M5-040 | behavior | `stdlib/memory/tests/README.md`:99 | The seed half of the finding misreads the code: jitter() returns immediately when max_us == 0, so jitter_seed() is only ever reached with jitter enabled — whenever a seed is drawn it is printed, exact… |
| A3-012 | test-link | `stdlib/ndarray/ndarray.hpp`:369<br>`ndarray::basic_ndarray::ndim` | @test links in this codebase are coverage links, not direct-call assertions — the same block's neighbours link tests that reach them through free functions. BroadcastingAdd calls nd::add, which reache… |
| A3-013 | test-link | `stdlib/ndarray/ndarray.hpp`:550<br>`ndarray::broadcast_shapes` | binary_op opens with broadcast_shapes(a.shape(), b.shape()) unconditionally, and the E2E program calls ndarray.add/sub/mul/divide, so StdlibE2E.Ndarray genuinely executes this function end to end. The… |
| A3-014 | test-link | `stdlib/ndarray/ndarray.hpp`:282<br>`ndarray::basic_ndarray::basic_ndarray()` | The E2E program's `let s = ndarray.scalar(2.0)` runs scalar(), whose first statement is `basic_ndarray<T> a;` — this very constructor. The systest file's own header says class methods are 'reached onl… |
| A3-015 | test-link | `stdlib/ndarray/ndarray.hpp`:296<br>`ndarray::basic_ndarray::basic_ndarray(shape, fill)` | The E2E program calls ndarray.zeros, ndarray.ones and ndarray.full, all of which construct through this (shape, fill) constructor, so the test really executes it. Indirect reach is the norm for these … |
| A3-016 | test-link | `stdlib/ndarray/ndarray.hpp`:317<br>`ndarray::basic_ndarray::uninitialized` | uninitialized() is called by array(), reshape() and binary_op's result allocation, all of which the E2E program drives (ndarray.array, ndarray.reshape, ndarray.add). The systest executes this static m… |
| A3-017 | test-link | `stdlib/ndarray/ndarray.hpp`:339<br>`ndarray::basic_ndarray::basic_ndarray(data, shape, strides, …` | ndarray.add(m, o) in the E2E program has two same-shape multi-element operands, so binary_op falls past both scalar fast paths into broadcast_to, which ends in exactly this view constructor. The tag i… |
| A3-018 | test-link | `stdlib/ndarray/ndarray.hpp`:352<br>`ndarray::basic_ndarray::shape` | shape() is read on essentially every line of the E2E program's execution — shape_of, binary_op's broadcast_shapes call, is_contiguous, to_string. The tag is true, and the finding's own note admits sha… |
| A3-019 | test-link | `stdlib/ndarray/ndarray.hpp`:361<br>`ndarray::basic_ndarray::strides` | is_contiguous reads a.strides() directly and runs on the E2E program's reshape and binary_op paths; broadcast_to reads it too. The systest executes strides() many times over, so the tag is a true cove… |
| A3-020 | test-link | `stdlib/ndarray/ndarray.hpp`:370<br>`ndarray::basic_ndarray::ndim` | Same as A3-018/A3-019: ndim() is called by broadcast_to, shape_of, reshape and to_string, all driven by the E2E program. Indirect execution is real execution; deleting the tag drops a true claim. |
| A3-021 | test-link | `stdlib/ndarray/ndarray.hpp`:382<br>`ndarray::basic_ndarray::size` | The E2E program calls ndarray.size_of(m), whose whole body is `return static_cast<long long>(a.size());`. That is a one-hop, unconditional execution of this member by the named test. The tag is true. |
| A3-024 | test-link | `stdlib/ndarray/ndarray.hpp`:465<br>`ndarray::basic_ndarray::at` | at() is executed by the E2E program twice over: ndarray.get(m, [1, 2]) calls a.at(to_size(index)) directly, and every ndarray.to_string(...) walks format_rec, which calls a.at(idx) per element. The ta… |
| A3-025 | test-link | `stdlib/ndarray/ndarray.hpp`:486<br>`ndarray::basic_ndarray::buffer` | buffer() runs on the E2E program's array(), reshape() and binary_op paths — array() writes through a.buffer()->begin(), reshape reads a.buffer()->data(), binary_op reads both operands' buffers. Execut… |
| A3-026 | test-link | `stdlib/ndarray/ndarray.hpp`:495<br>`ndarray::basic_ndarray::offset` | offset() is read by reshape's contiguous fast path, by binary_op's transform bounds and by broadcast_to when it builds the view — all on the E2E program's path. The systest executes it; deleting the t… |
| A3-027 | test-link | `stdlib/ndarray/ndarray.hpp`:571<br>`ndarray::is_contiguous` | reshape calls is_contiguous(a) on every invocation and binary_op calls it up to three times, both driven by the E2E program's ndarray.reshape and ndarray.add. The function is genuinely executed by Std… |
| A3-028 | test-link | `stdlib/ndarray/ndarray.hpp`:599<br>`ndarray::broadcast_to` | ndarray.add(m, o) in the E2E program has two same-shape multi-element operands, so binary_op skips both size-1 fast paths and calls broadcast_to on each operand. The function is executed by the named … |
| A3-029 | complexity | `stdlib/ndarray/ndarray.hpp`:837<br>`ndarray::reshape` | The module's settled convention prices an odometer walk as O(size): add/sub/mul/divide all fall back to the same at()-per-element loop in binary_op and are documented '@complexity O(size of result)'. … |
| A3-030 | complexity | `stdlib/ndarray/ndarray.hpp`:878<br>`ndarray::astype` | Identical to A3-029: astype's strided branch is the same at()-per-element odometer the elementwise ops use, and those are all documented O(size). Changing astype alone would create an inconsistency in… |
| A10-025 | concurrency | `stdlib/os/os.hpp`:214<br>`os::system` | Unlike getenv/setenv, the blocking fact is already documented here: the detail paragraph says std::system 'blocks until it finishes' and @complexity already charges 'the cost of the spawned process (f… |
| A5-013 | behavior | `stdlib/parsers/json/json.hpp`:105<br>`parsers::json::Parser::parse` | Parser::dump touches only dump_buf_, so a parsed Document does survive a dump() — but that makes the method doc a NARROWER promise than the implementation, not a false statement: nothing is valid for … |
| M3-012 | behavior | `stdlib/regex/README.md`:107 | The documented claim is accurate: `(?` is rejected by parse_atom's metacharacter guard, and `\1` falls through escape_class to set_bit as the literal '1' — neither backreferences nor lookaround are im… |
| A13-004 | test-link | `stdlib/regex/regex.hpp`:60<br>`cheatah::regex::compile` | Nothing documented is false — the finding's own note concedes it. Both named systests exist and compile a pattern (regex_e2e_test.cpp:201-202 calls regex.compile twice). No contract rule requires a @t… |
| A13-005 | test-link | `stdlib/regex/regex.hpp`:82<br>`cheatah::regex::search` | Both systests exist and call regex.search — SearchPresentAbsent at regex_e2e_test.cpp:27-28 and ReDoSNestedQuantifierReturnsFalseFast at :92. The documented claim is accurate; a missing @test is not a… |
| A13-006 | test-link | `stdlib/regex/regex.hpp`:98<br>`cheatah::regex::full_match` | RegexE2E.FullMatchIsAnchoredBothEnds exists and calls regex.full_match three times, so the tag is true. The finding records FALSE only to attach a same-named unit test; the contract does not require @… |
| A13-007 | test-link | `stdlib/regex/regex.hpp`:133<br>`cheatah::regex::find` | Both systests exist and call regex.find (regex_e2e_test.cpp:51 and :64). Nothing documented is false. Adding Regex.FindBudgetFallbackOnDenseAbsentInput would pair well with the A13-001 complexity fix,… |
| A14-011 | brief | `stdlib/requests/requests.purr`:829<br>`requests::get` | Nothing here is false: RequestsSys.BasicGet exists at requests_sys_test.cpp:34 and drives requests.get end to end, and the @complexity/@alloc values hold. The defect is purely that brief plus two @par… |
| A8-004 | test-link | `stdlib/socket/socket.hpp`:358<br>`socket::Conn::~Conn` | ~Conn does execute in the named test: `c` is destroyed at scope exit (socket_test.cpp:269) after its explicit close, so the tag names a real TEST that runs the destructor. The contract requires @test … |
| A8-005 | test-link | `stdlib/socket/socket.hpp`:493<br>`socket::Listener::~Listener` | Same as A8-004: ListenerLoopback constructs three Listeners (a, b, c) that are all destroyed at scope exit, so ~Listener runs in the named test. Branch coverage of the fd_>=0 arm is not what the @test… |

### Leads chased rather than waved through

- **`fixarray::min` / `max` briefs.** `A6-002`'s dissent contrasted "unlike `min`", which
  implied `min`'s brief pointed the wrong way. It no longer does: `A6-001` was accepted and
  corrected it ("floored" → "capped") in `f39ad05`. Both briefs now match the test —
  `min({1,5,9}, 4) == {1,4,4}` caps, `max` raises.
- **`parsers::json::Parser::parse` invalidation.** The doc promises the result is valid only
  until the next `parse` **or `dump()`**. `dump()` writes only `dump_buf_`
  (`stdlib/parsers/json/json.hpp:174`), separate from the parse pool, so a `Document` does
  survive a dump. That makes the documented contract *narrower* than the implementation, not
  false — and tightening it would pin an implementation detail the module may want to keep
  free. Left as written, deliberately.
- **`linalg` scalar-out kernels** (`A1-028`, `A1-029`, `A1-030`). The `det`, `norm` and
  `slogdet` host kernels carry no doc comment at all — but they sit inside `/// @cond INTERNAL`,
  so Doxygen never emits them and the tag gates do not cover them. An omission in hidden code,
  not a false claim.


## Found while verifying: a blind spot in the lint itself

Closing out the audit turned up a defect the audit could not have caught, because it was in the
gate doing the catching.

`scripts/doc_lint.sh tests` only recognised `@test` when the tag began a stripped comment line.
cheatah's house style puts `@complexity` and `@alloc` on one line, and a `@test` often rides
along behind them — so **103 tags were never checked**. Of those, **31 references (13 distinct
names) named tests that do not exist**, all in the JSON module's internal headers: `Json.*` and
`JsonRead.*`, where the real suites are `CheatahParsersJson.*` and `ParsersJsonDom.*`.

Three things came out of it:

- The lint now finds the tag anywhere on the line. It checks 1,462 names, up from 1,394.
- All 31 dead references were repointed at tests that genuinely exercise the function — each
  checked by reading the test, not by matching names.
- One of them, `JsonRead.FixedArrays` on `read_fixed_array`, named a test that had never been
  written: no test read a `std::array` field, so the template was never instantiated and the
  coverage numbers never noticed. `CheatahParsersJson.TypedReadFixedArray` now covers the exact
  fill plus the too-few and too-many rejections, and was confirmed able to fail by suppressing
  the element store.

The pattern is worth remembering: a documentation gate that silently skips input reports 100%
just as loudly as one that checks everything.
