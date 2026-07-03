#pragma once

/**
 * @file request.hpp
 * @brief `memory::Request<Lease>` — a promise for a lease; `.acquire(on_interrupt)` blocks.
 *
 * What `Owner::rread()` / `rwrite<…>()` return. Move-only; wraps a
 * `std::future<Lease>` (the owner fulfills the matching `std::promise` when it grants). Obtaining a
 * `Request` does NOT block — you hold "a request for a lease". `acquire()` is the single blocking
 * redemption; the future/promise never surface in cheatah. No `friend`: the interrupt handler is
 * attached through the lease's public `on_interrupt()`.
 */

#include <functional>
#include <future>
#include <utility>

#include "lease.hpp"

namespace cheatah::memory {

template <class LeaseT>
class Request {
public:
    /// Wrap the owner-supplied future. @complexity O(1). @alloc none.
    explicit Request(std::future<LeaseT>&& fut) noexcept : fut_(std::move(fut)) {}
    Request(Request&&) noexcept = default;
    Request& operator=(Request&&) noexcept = default;
    Request(const Request&) = delete;

    /**
     * BLOCK until the owner grants, then return the lease. @p on_interrupt is the requester's
     * interruption handler — wired to the granted lease's stop token, so if the owner later needs the
     * lease back the handler fires. Omit it to rely purely on the lease's `valid()`/`expired()`
     * polling. One-shot: acquire once per request.
     * @complexity O(1) once granted; **blocks** until the owner fulfills the request.
     * @alloc none, unless @p on_interrupt is supplied — then one `std::stop_callback` on the heap
     *        (via `Lease::on_interrupt`).
     */
    LeaseT acquire(std::function<void()> on_interrupt = {}) {
        LeaseT lease = fut_.get();                       // block until the owner fulfills the promise
        if (on_interrupt) lease.on_interrupt(std::move(on_interrupt));
        return lease;                                    // moved (NRVO) into the caller's Lease
    }

private:
    std::future<LeaseT> fut_;   ///< owner-fulfilled; never exposed to cheatah.
};

}  // namespace cheatah::memory
