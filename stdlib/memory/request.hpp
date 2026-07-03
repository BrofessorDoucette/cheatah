// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
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

/**
 * @brief A promise for a lease — what `Owner::rread()`/`rwrite<…>()` return. Move-only; block for the
 * lease with `acquire()`. @tparam LeaseT the `Lease` type this request resolves to.
 */
template <class LeaseT>
class Request {
public:
    /// Wrap the owner-supplied future. @param fut the future the owner fulfills. @complexity O(1). @alloc none.
    explicit Request(std::future<LeaseT>&& fut) noexcept : fut_(std::move(fut)) {}
    /// Move-construct (a request is move-only).
    Request(Request&&) noexcept = default;
    /// Move-assign (a request is move-only). @return `*this`.
    Request& operator=(Request&&) noexcept = default;
    Request(const Request&) = delete;

    /**
     * BLOCK until the owner grants, then return the lease. @p on_interrupt is the requester's
     * interruption handler — wired to the granted lease so if the owner later needs the lease back the
     * handler fires. Omit it to rely purely on the lease's `valid()`/`expired()` polling. One-shot.
     * @param on_interrupt optional handler to run when the owner asks the lease to yield.
     * @return the granted lease.
     * @complexity O(1) once granted; **blocks** until the owner fulfills the request.
     * @alloc none, unless @p on_interrupt is supplied — then one callback holder (via `Lease::on_interrupt`).
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
