// Copyright (c) 2026 BigBrain LLC. MIT-licensed (see LICENSE).
// Original work; see ACKNOWLEDGMENTS.md for the open-source ideas we build upon.
#pragma once

/**
 * @file policy.hpp
 * @brief `memory` scheduling policy + the immediate-write priority constant.
 */

#include <cstdint>

namespace cheatah::memory {

/// How the owner schedules pending writes against reads. Chosen at construction (a stored value).
enum class policy : std::uint8_t {
    interleave,    ///< (default) readers renew *between* writes; writers + waiting readers are fair.
    writes_first,  ///< drain the ENTIRE write queue before renewing any reader (owner's declared choice).
};
/// Shorthand for `policy::interleave` — readers renew between writes (the default, fair) policy.
inline constexpr policy interleave   = policy::interleave;
/// Shorthand for `policy::writes_first` — drain the whole write queue before renewing any reader.
inline constexpr policy writes_first = policy::writes_first;

/// The readable spelling of an immediate-write priority. Any negative priority is immediate; this is
/// simply its name: `x.rwrite<memory::immediate>()` reads better than `x.rwrite<-1>()`.
inline constexpr long long immediate = -1;

}  // namespace cheatah::memory
