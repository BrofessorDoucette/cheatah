// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file concepts.hpp
 * @brief cheatah `linalg` — the concept layer the algorithms are written against.
 *
 * The routines in routines.hpp used to be typed concretely against `NDArray` /
 * `CNDArray`, with real and complex handled by hand-duplicated overloads. This header
 * introduces the STL-`std::ranges`-style concept surface that lets a single algorithm
 * template serve **any** conforming container and element type, and adds an orthogonal
 * **location** axis so a container knows whether it lives on the host or on a device.
 *
 * Two axes sit on top of the element ladder already defined in ndarray.hpp
 * (`Numeric ⊂ FloatingPoint`, `Field`, `Element`, `Copyable`):
 *   - an **access/shape** surface — @ref ArrayLike / @ref NumericArray / @ref FloatArray —
 *     defined structurally so both a host `basic_ndarray` and a device array satisfy it;
 *   - a **location** axis — @ref host_location + the @ref location_of trait, yielding
 *     @ref HostArray / @ref DeviceArray / @ref SameLocation — so that mixing a host and a
 *     device operand in one operation is an **unsatisfied constraint (a compile error)**,
 *     never a runtime check.
 *
 * cheatah defines only the host tag here; a GPU extension provides its own location tag
 * and specializes @ref location_of for its own container — so the concept vocabulary is
 * shared without this public header ever naming the (private) extension. Element data is
 * reached only through location-specific customization points (see backend.hpp), so the
 * surface below is deliberately the common denominator: host-side metadata only.
 *
 * This header adds no runtime behavior; it is pure compile-time vocabulary.
 */
#include <complex>
#include <concepts>
#include <cstddef>
#include <type_traits>
#include <vector>

#include "ndarray.hpp"

namespace cheatah::linalg {

/// element_t<A>: the scalar an array-like stores — its nested `value_type`, with any
/// reference/cv-qualification on `A` stripped first so `const NDArray&` and `NDArray`
/// yield the same element type. Undefined for a type with no `value_type` (which simply
/// makes the concepts below unsatisfied for it, never a hard error).
template <class A>
using element_t = typename std::remove_cvref_t<A>::value_type;

/// ArrayLike<A>: the structural surface every linalg-capable container exposes — a stored
/// @ref cheatah::ndarray::Element plus host-side shape metadata (`shape`, `strides`,
/// `ndim`, `size`, `offset`). Defined à la `std::ranges` (by shape, not by inheritance) so
/// a host `basic_ndarray` and a device array both model it. The member surface is checked
/// FIRST, so a non-array type (e.g. `int`) fails here before `element_t` is ever consulted.
template <class A>
concept ArrayLike = requires(const std::remove_cvref_t<A>& a) {
    typename std::remove_cvref_t<A>::value_type;
    { a.shape() } -> std::convertible_to<const std::vector<std::size_t>&>;
    { a.strides() } -> std::convertible_to<const std::vector<std::ptrdiff_t>&>;
    { a.ndim() } -> std::convertible_to<std::size_t>;
    { a.size() } -> std::convertible_to<std::size_t>;
    { a.offset() } -> std::convertible_to<std::size_t>;
} && ndarray::Element<element_t<A>>;

/// NumericArray<A>: an @ref ArrayLike whose element is a @ref cheatah::ndarray::Field
/// (a real or complex number). This is the bound the arithmetic routines share; it is what
/// unifies today's separate `NDArray` and `CNDArray` overloads into one constrained
/// template (real vs complex becomes a compile-time branch, not a duplicated signature).
template <class A>
concept NumericArray = ArrayLike<A> && ndarray::Field<element_t<A>>;

/// FloatArray<A>: a @ref NumericArray whose element's REAL BASE is floating point — the
/// bound for routines that need division / √ (solve, inv, det, qr, svd, eig). Constraining
/// the real base (not the element itself) admits complex containers too, so a Hermitian
/// complex `eigh` is allowed while an integer array is cleanly rejected.
template <class A>
concept FloatArray = NumericArray<A> && ndarray::FloatingPoint<ndarray::real_base_t<element_t<A>>>;

/// host_location: the location tag for a container whose element buffer lives in ordinary
/// host (CPU) memory. cheatah defines only this tag; a device extension defines its own
/// (e.g. a `device_location`) and specializes @ref location_of for its container type.
struct host_location {};

/// location_of<A>: the trait naming where an array-like's elements live. Left undefined for
/// an unknown container (so @ref Located is simply false for it); specialized below for the
/// host `basic_ndarray`, and specialized by an extension for its own device container. Being
/// a trait rather than a member keeps `basic_ndarray` itself untouched by this axis.
template <class A>
struct location_of;

/// @cond INTERNAL
template <ndarray::Element T>
struct location_of<ndarray::basic_ndarray<T>> {
    using type = host_location;
};
/// @endcond

/// location_t<A>: shorthand for the location tag of `A` (its @ref location_of `::type`),
/// with any reference/cv-qualification stripped from `A` first.
template <class A>
using location_t = typename location_of<std::remove_cvref_t<A>>::type;

/// Located<A>: an @ref ArrayLike that also advertises a location (its @ref location_of is
/// specialized). Gates the location-aware concepts below so an un-tagged type fails cleanly
/// rather than hard-erroring on a missing `location_of` specialization.
template <class A>
concept Located = ArrayLike<A> && requires { typename location_t<A>; };

/// HostArray<A>: a @ref Located container whose elements live in host memory. The host
/// `basic_ndarray` (`NDArray`, `CNDArray`) models this; it is the domain of the CPU kernels.
template <class A>
concept HostArray = Located<A> && std::same_as<location_t<A>, host_location>;

/// DeviceArray<A>: a @ref Located container whose elements do NOT live in host memory — i.e.
/// on a GPU/accelerator. Defined structurally as "located and not host" so this public
/// header can name the CONCEPT without naming any specific device type; an extension's
/// container satisfies it automatically once it specializes @ref location_of.
template <class A>
concept DeviceArray = Located<A> && !std::same_as<location_t<A>, host_location>;

/// SameLocation<A,B>: the compile-time firewall — two operands share a location. A host⊗device
/// (or device⊗host) call fails to satisfy this, so it is rejected at compile time with a clean
/// concept error instead of a runtime guard. Every binary/ternary routine carries this bound.
template <class A, class B>
concept SameLocation = Located<A> && Located<B> && std::same_as<location_t<A>, location_t<B>>;

/// SameField<A,B>: two operands share a real base (real·real or complex·complex over the same
/// floating type). Mixing e.g. an `f64` and an `f32` container, or real with complex, is not
/// silently promoted — it is a compile error, and promotion must be an explicit conversion.
template <class A, class B>
concept SameField = std::same_as<ndarray::real_base_t<element_t<A>>, ndarray::real_base_t<element_t<B>>>;

}  // namespace cheatah::linalg
