#pragma once

// cheatah-deps:

/**
 * @file sensor.hpp
 * @brief `import lab.sensor` — a FIXTURE user package for the extension tests: the common shape
 *        of a token-based module (a descriptor struct, an optional-returning opener, a closer),
 *        so hover/definition behavior is pinned for every package built this way.
 */

#include <cstdint>
#include <optional>

namespace lab::sensor {

/**
 * What to open the sensor FOR — the opener's only knobs.
 */
struct SensorDesc {
    /// Stream continuously instead of one-shot sampling.
    bool streaming = false;
    /// Labels to attach, as a borrowed array; null when @ref tag_count is 0.
    const char* const* tags = nullptr;
    /// Number of entries in @ref tags.
    long long tag_count = 0;
};

/**
 * An open sensor: plain data, native handles as `long long` tokens.
 */
struct Sensor {
    /// The device handle token; 0 = not open.
    long long handle = 0;
    /// The calibration epoch the device reported.
    long long epoch = 0;
};

/**
 * Open the first available sensor.
 *
 * @param desc what to open the sensor for; the default is one-shot.
 * @return the open @ref Sensor, or `std::nullopt` when no sensor exists.
 * @complexity O(devices).
 * @alloc none.
 * @test Sensor.Open
 */
inline std::optional<Sensor> open_sensor(const SensorDesc& desc = {}) {
    (void)desc;
    return std::nullopt;
}

/**
 * Close an open sensor; zeroed on return so a double-close is a no-op.
 *
 * @param s the sensor to close.
 * @complexity O(1).
 * @alloc none.
 * @test Sensor.Close
 */
inline void close_sensor(Sensor& s) {
    s = Sensor{};
}

}  // namespace lab::sensor

namespace cheatah { namespace lab = ::lab; }
