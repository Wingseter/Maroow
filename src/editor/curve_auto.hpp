#pragma once

#include <array>
#include <optional>
#include <vector>

namespace marrow::editor::curve_auto {

/** @brief One (time, driver value) sample of a track, in ascending time. */
struct Sample {
    double time_seconds{0.0};
    double value{0.0};
};

/** Every automatic curve has these two x control points; see the design §6.2. */
inline constexpr double kAutoControlPointX1 = 1.0 / 3.0;
inline constexpr double kAutoControlPointX2 = 2.0 / 3.0;
/** Shortest segment whose normalized time axis still has usable extent. */
inline constexpr double kMinimumSegmentSeconds = 1e-6;

/**
 * @brief Monotone Fritsch-Carlson control points, one per outgoing segment.
 *
 * Returns `samples.size() - 1` entries, or an empty vector for fewer than two
 * samples. Returns `std::nullopt` when any time or value is non-finite, when
 * times are not strictly increasing, or when any segment is shorter than
 * `kMinimumSegmentSeconds`, because a zero-extent normalized time axis has no
 * honest fallback. Every returned entry has `cx1 == kAutoControlPointX1`,
 * `cx2 == kAutoControlPointX2`, and `cy1`, `cy2` inside `[0, 1]`, so the result
 * satisfies the `.marrow`/`.mskl` `cx in [0, 1]` invariant unconditionally and
 * can never overshoot its segment endpoints.
 */
std::optional<std::vector<std::array<double, 4>>> segment_control_points(
    const std::vector<Sample>& samples);

} // namespace marrow::editor::curve_auto
