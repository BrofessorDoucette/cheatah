#pragma once

/**
 * @file lease.hpp
 * @brief `memory::Lease<T, Mode>` — the only handle to an owned object.
 *
 * Move-only RAII. A lease does NOT hold a `std::lock`; the `Owner` coordinator grants logical
 * exclusion (readers share, a writer is alone) and the lease just carries a **release callback** it
 * fires on destruction to tell the owner "I'm done". Access:
 *   - `r.read()`            → `const T&` (a REFERENCE to the owned object — never a copy or raw pointer).
 *   - `w.write(value)`      → SETTER: replace the whole object. `write` NEVER returns an object.
 *   - `w.write(index, v)`   → set element `index` (Indexed sequences: vector/array/string/ndarray).
 *   - `w.write(key, v)`     → set `key`           (Mapping containers: map/unordered_map/dict).
 * `valid()`/`expired()` track a shared `Gate` the owner flips to ask the holder to yield (drain, or an
 * immediate-write preempt); `on_interrupt(fn)` fires `fn` once when that first happens.
 */

#include <atomic>
#include <concepts>
#include <cstddef>
#include <functional>
#include <memory>
#include <utility>

#include "mode.hpp"
#include "ownable.hpp"

namespace cheatah::memory {

namespace detail {
/// Shared signal between the owner and a lease. The owner flips `valid` false to ask the holder to
/// yield; the holder sets `acked` when it observes that (so the owner knows it has paused) and calls
/// `wake` so the owner's condition variable re-checks (the holder acks off the owner's mutex).
struct Gate {
    std::atomic<bool>     valid{true};
    std::atomic<bool>     acked{false};
    std::function<void()> wake;   ///< set by the owner to notify its cv_ when the holder first acks.
};
}  // namespace detail

template <Ownable T, Mode M>
class Lease {
public:
    /// The owner's grant path (called only by `Owner`): the object pointer, the generation gate, and
    /// the release callback fired on destruction. @complexity O(1). @alloc none.
    Lease(T* obj, std::shared_ptr<detail::Gate> gate, std::function<void()> release) noexcept
        : obj_(obj), gate_(std::move(gate)), release_(std::move(release)) {}

    Lease(Lease&& o) noexcept { steal(o); }
    Lease& operator=(Lease&& o) noexcept { if (this != &o) { drop(); steal(o); } return *this; }
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
    ~Lease() { drop(); }

    /// Read the whole object. Available on every lease. A REFERENCE (`const T&`) — never a copy or `T*`.
    /// @complexity O(1). @alloc none.
    const T& read() const { return *obj_; }

    /// Read element @p index of an Indexed sequence: `r.read(i)`. Mirrors `w.write(i, v)`. A REFERENCE.
    /// @complexity `T::operator[]`. @alloc none.
    const auto& read(std::size_t index) const
        requires Indexed<T>
    { return (*obj_)[index]; }

    /// Read the value at @p key of a Mapping: `r.read(k)`. Mirrors `w.write(k, v)`. Throws if absent
    /// (reading a missing key never inserts). A REFERENCE. @complexity `T::at`. @alloc none.
    template <class K>
        requires (Mapping<T> && std::convertible_to<K, typename T::key_type>)
    const auto& read(K&& key) const
    { return (*obj_).at(std::forward<K>(key)); }

    /// Read the first element (containers with `front()`: vector / deque / list / string …). A REFERENCE.
    /// @complexity O(1). @alloc none.
    const auto& read_front() const
        requires HasFront<T>
    { return (*obj_).front(); }

    /// Read the last element (containers with `back()`). A REFERENCE. @complexity O(1). @alloc none.
    const auto& read_back() const
        requires HasBack<T>
    { return (*obj_).back(); }

    /// Replace the whole object: `w.write(value)`. The primary write form. Write / write_renewable only.
    /// @complexity O(1) plus assigning @p value. @alloc whatever `T`'s assignment allocates.
    void write(T value)
        requires is_write_mode<M>
    { *obj_ = std::move(value); }

    /// Set element @p index of an Indexed sequence: `w.write(i, v)`. Deduced. Write modes only.
    /// @complexity `T::operator[]`. @alloc whatever the element assignment allocates.
    template <class V>
        requires (is_write_mode<M> && Indexed<T>)
    void write(std::size_t index, V&& value)
    { (*obj_)[index] = std::forward<V>(value); }

    /// Set @p key of a Mapping: `w.write(k, v)`. Deduced. Write modes only.
    /// @complexity `T::operator[]` (may insert). @alloc whatever the insert/assignment allocates.
    template <class K, class V>
        requires (is_write_mode<M> && Mapping<T> &&
                  std::convertible_to<K, typename T::key_type>)
    void write(K&& key, V&& value)
    { (*obj_)[std::forward<K>(key)] = std::forward<V>(value); }

    /// Still ours? `true` until the owner asks us to yield (a writer waiting; an immediate-write). The
    /// holder observing `!valid()` is how the owner learns it has paused. @complexity O(1). @alloc none.
    bool valid() const noexcept {
        const bool v = gate_->valid.load(std::memory_order_acquire);
        if (!v) {
            if (!gate_->acked.exchange(true, std::memory_order_acq_rel) && gate_->wake)
                gate_->wake();  // first ack: wake the owner's cv_ so it re-checks (we ack off its mutex)
            if (on_interrupt_ && !fired_) { fired_ = true; on_interrupt_(); }
        } else {
            fired_ = false;  // reset so a later preempt (write resume then re-preempt) can fire again
        }
        return v;
    }
    /// Asked to yield? The negation of valid(). @complexity O(1). @alloc none.
    bool expired() const noexcept { return !valid(); }

    /// Register the "what to do if the owner interrupts me" handler; fires once, in the holder's thread,
    /// the first time `valid()` observes the stop. Replaces any previous handler. @complexity O(1).
    void on_interrupt(std::function<void()> handler) { on_interrupt_ = std::move(handler); }

private:
    void drop() noexcept { if (release_) { auto r = std::move(release_); release_ = nullptr; r(); } }
    void steal(Lease& o) noexcept {
        obj_ = o.obj_; gate_ = std::move(o.gate_); release_ = std::move(o.release_);
        on_interrupt_ = std::move(o.on_interrupt_); fired_ = o.fired_;
        o.obj_ = nullptr; o.release_ = nullptr;
    }

    T*                             obj_{};        ///< direct pointer to the object (one deref).
    std::shared_ptr<detail::Gate>  gate_;         ///< shared yield signal (per generation / active write).
    std::function<void()>          release_;      ///< tells the owner "I'm done" (fired once, in dtor).
    std::function<void()>          on_interrupt_; ///< optional push handler when asked to yield.
    mutable bool                   fired_ = false;///< has on_interrupt_ fired for the current stop?
};

}  // namespace cheatah::memory
