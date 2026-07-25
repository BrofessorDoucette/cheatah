// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once
// cheatah-link: -pthread

/**
 * @file memory.hpp
 * @brief `memory` — cheatah's ownership vocabulary. Umbrella header: `import memory`.
 *
 * Implemented and shipping (see DESIGN.md for the rationale, tests/ for the behavioural contract).
 *
 * A thin layer over standard-library primitives — one `std::mutex` + `std::condition_variable`
 * coordinator over explicit reader/writer state, a `std::priority_queue` of waiting writes, and
 * `std::promise`/`std::future` for the request → acquire handshake. Memory safety comes from *what* is
 * handed back — a lease you can only `read()`/`write(...)` through — not from hiding the plumbing.
 * **No GC. No copy/clone. No hidden copies of the owned object** — the only heap the coordinator
 * touches is its own small bookkeeping (gates, promise/future state, the write queue). This
 * Owner/Lease/Request model is the real
 * ownership/borrow system that supersedes the earlier `view<T>` stopgap (e.g. regex's `view<str>`).
 *
 * Split across focused headers (this umbrella just includes them):
 *   - mode.hpp     — access-mode tags `read` / `write` / `write_renewable` + the `Mode` concept.
 *   - policy.hpp   — the `policy` enum (`interleave` / `writes_first`) + the `immediate` (== -1) constant.
 *   - lease.hpp    — `Lease<T, Mode>`  (the only handle; read()/write(); valid()/expired()).
 *   - request.hpp  — `Request<Lease>`  (what accessors return; `.acquire(on_interrupt)` blocks -> Lease).
 *   - owner.hpp    — `Owner<T>` + `own()` (sole owner + coordinator; rread() / rwrite<priority>()).
 *
 * Flow:   Owner<T> x = own(v);   Request<Lease<T,read>> r = x.rread();   Lease<T,read> l = r.acquire(cb);
 * Policy: memory.interleave (default) | memory.writes_first          Immediate: memory.immediate (== -1)
 */

#include "mode.hpp"
#include "policy.hpp"
#include "lease.hpp"
#include "request.hpp"
#include "owner.hpp"
