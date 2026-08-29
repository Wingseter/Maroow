#include "curve_auto.hpp"

#include <cmath>
#include <cstddef>
#include <limits>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor::curve_auto {

namespace {

// The design's §6.2 proof rests on both constants narrowing strictly inside
// (0, 1) with cx2 > cx1, which is what keeps the runtime's X(t) = alpha inverse
// well posed and satisfies the `cx in [0, 1]` invariant both loaders enforce.
static_assert(
    static_cast<runtime::AnimationScalar>(kAutoControlPointX1) > 0.0f,
    "the automatic cx1 must narrow above 0");
static_assert(
    static_cast<runtime::AnimationScalar>(kAutoControlPointX1) < 1.0f,
    "the automatic cx1 must narrow below 1");
static_assert(
    static_cast<runtime::AnimationScalar>(kAutoControlPointX2) > 0.0f,
    "the automatic cx2 must narrow above 0");
static_assert(
    static_cast<runtime::AnimationScalar>(kAutoControlPointX2) < 1.0f,
    "the automatic cx2 must narrow below 1");
static_assert(
    kAutoControlPointX1 < kAutoControlPointX2,
    "the automatic x control points must stay ordered");

/** @brief The tangent-ratio bound of the Fritsch-Carlson disk `a^2 + b^2 <= 9`. */
constexpr double kTangentDiskRadius = 3.0;

/** @brief Maps `-0.0` onto `+0.0` so no stored curve ever serializes as `-0`. */
double normalize_zero(double value) {
    return value == 0.0 ? 0.0 : value;
}

bool finite_animation_scalar(double value) {
    if (!std::isfinite(value)) return false;
    const double narrowed =
        static_cast<double>(static_cast<runtime::AnimationScalar>(value));
    return std::isfinite(narrowed);
}

} // namespace

std::optional<std::vector<std::array<double, 4>>> segment_control_points(
    const std::vector<Sample>& samples) {
    const std::size_t count = samples.size();
    if (count < 2U) {
        // No outgoing segment exists. This is not an error: a one-key track
        // simply has nothing to resolve.
        return std::vector<std::array<double, 4>>{};
    }

    const std::size_t segment_count = count - 1U;

    // Pass A - secants. Every intermediate is checked, not only the output,
    // because `inf - inf` produces NaN and NaN passes a naive range test.
    std::vector<double> h(segment_count, 0.0);
    std::vector<double> d(segment_count, 0.0);
    for (std::size_t index = 0U; index < count; ++index) {
        if (!std::isfinite(samples[index].time_seconds) ||
            !std::isfinite(samples[index].value)) {
            return std::nullopt;
        }
    }
    for (std::size_t index = 0U; index < segment_count; ++index) {
        const double span =
            samples[index + 1U].time_seconds - samples[index].time_seconds;
        if (!std::isfinite(span) || span <= kMinimumSegmentSeconds) {
            // A zero-extent normalized time axis has no honest fallback, and a
            // non-increasing pair is malformed, so the whole track is rejected.
            return std::nullopt;
        }
        const double rise = samples[index + 1U].value - samples[index].value;
        const double secant = rise / span;
        if (!std::isfinite(rise) || !std::isfinite(secant)) {
            return std::nullopt;
        }
        h[index] = span;
        d[index] = secant;
    }

    // Pass B - three-point tangents. The arithmetic mean of the two adjacent
    // secants, one-sided at both ends. The Fritsch-Butland harmonic variant is
    // deliberately not used: the pass D clamp already delivers monotonicity.
    std::vector<double> m(count, 0.0);
    m[0] = d[0];
    m[count - 1U] = d[segment_count - 1U];
    for (std::size_t index = 1U; index + 1U < count; ++index) {
        m[index] = (d[index - 1U] + d[index]) / 2.0;
        if (!std::isfinite(m[index])) return std::nullopt;
    }

    // Pass C - flat zeroing over ALL segments before the clamp runs. This is
    // what makes a plateau actually flat instead of letting the interpolant
    // bulge through it, and it flattens the ends of the neighbouring segments.
    for (std::size_t index = 0U; index < segment_count; ++index) {
        if (d[index] == 0.0) {
            m[index] = 0.0;
            m[index + 1U] = 0.0;
        }
    }

    // Pass D - the monotonicity clamp, in ascending `i`, mutating `m` in place.
    //
    // Why the ascending in-place clamp is correct: segment `i` is the last
    // iteration that touches `m[i]`, so after iteration `i` completes `m[i]` is
    // final, while `m[i+1]` may still be shrunk by iteration `i+1`. Every write
    // here moves a tangent strictly toward zero, and the admissible region
    // restricted to the first quadrant - `a >= 0`, `b >= 0`, `a^2 + b^2 <= 9` -
    // is downward closed, so segment `i`'s guarantee survives every later
    // shrink. Pass E therefore reads one consistent final array, C1 continuity
    // is preserved because both sides of key `i` read the same `m[i]`, and the
    // result depends on nothing but the input. Do not "fix" this into a
    // per-segment clamp that never writes back: that would break C1.
    for (std::size_t index = 0U; index < segment_count; ++index) {
        const double secant = d[index];
        if (secant == 0.0) continue;
        // `a < 0` and `b < 0` as exact sign comparisons rather than as the
        // ratio, because the ratio can overflow (see the scale below).
        if ((m[index] < 0.0) != (secant < 0.0)) m[index] = 0.0;
        if ((m[index + 1U] < 0.0) != (secant < 0.0)) m[index + 1U] = 0.0;

        // `hypot(a, b) > 3` and the scale `s = 3 / hypot(a, b)`, evaluated
        // without ever forming `a` or `b`. Algebraically identical to
        // `m = s * a * d`, because `hypot(a, b) = hypot(m[i], m[i+1]) / |d|`,
        // so `s * m = 3 * |d| * (m / hypot(m[i], m[i+1]))`. The re-association
        // matters: a legitimately enormous ratio - reachable when one secant is
        // denormal-small and its neighbour is huge - overflows to infinity in
        // the direct form and collapses both tangents to NaN, while every
        // quantity below stays bounded by `3 * |d|`.
        const double tangent_norm = std::hypot(m[index], m[index + 1U]);
        const double secant_magnitude = std::abs(secant);
        if (!std::isfinite(tangent_norm)) return std::nullopt;
        if (tangent_norm > kTangentDiskRadius * secant_magnitude) {
            const double bound = kTangentDiskRadius * secant_magnitude;
            m[index] = bound * (m[index] / tangent_norm);
            m[index + 1U] = bound * (m[index + 1U] / tangent_norm);
        }
    }

    // Pass E - control points from the FINAL tangent array.
    std::vector<std::array<double, 4>> control_points;
    control_points.reserve(segment_count);
    for (std::size_t index = 0U; index < segment_count; ++index) {
        double cy1 = kAutoControlPointX1;
        double cy2 = kAutoControlPointX2;
        if (d[index] != 0.0) {
            const double a = m[index] / d[index];
            const double b = m[index + 1U] / d[index];
            if (!std::isfinite(a) || !std::isfinite(b)) return std::nullopt;
            cy1 = a / 3.0;
            cy2 = 1.0 - b / 3.0;
        }
        // A no-op in exact arithmetic (pass D bounds `a` and `b` into [0, 3]),
        // and a hard guard against a value that lands one ULP outside the
        // `a^2 + b^2 = 9` boundary after the scale.
        cy1 = normalize_zero(std::fmin(std::fmax(cy1, 0.0), 1.0));
        cy2 = normalize_zero(std::fmin(std::fmax(cy2, 0.0), 1.0));
        if (!finite_animation_scalar(cy1) || !finite_animation_scalar(cy2)) {
            return std::nullopt;
        }
        const double narrowed_cy1 =
            static_cast<double>(static_cast<runtime::AnimationScalar>(cy1));
        const double narrowed_cy2 =
            static_cast<double>(static_cast<runtime::AnimationScalar>(cy2));
        if (narrowed_cy1 < 0.0 || narrowed_cy1 > 1.0 || narrowed_cy2 < 0.0 ||
            narrowed_cy2 > 1.0) {
            return std::nullopt;
        }
        control_points.push_back(
            {kAutoControlPointX1, cy1, kAutoControlPointX2, cy2});
    }

    return control_points;
}

} // namespace marrow::editor::curve_auto
