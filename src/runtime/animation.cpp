#include "marrow/runtime/animation.hpp"

#include <algorithm>
#include <cmath>

namespace marrow::runtime {
namespace {

double clamp_unit(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double sample_cubic_component(double control_point_1, double control_point_2, double t) {
    const double inverse_t = 1.0 - t;
    return 3.0 * inverse_t * inverse_t * t * control_point_1 +
           3.0 * inverse_t * t * t * control_point_2 + t * t * t;
}

double sample_cubic_derivative(double control_point_1, double control_point_2, double t) {
    const double inverse_t = 1.0 - t;
    return 3.0 * inverse_t * inverse_t * control_point_1 +
           6.0 * inverse_t * t * (control_point_2 - control_point_1) +
           3.0 * t * t * (1.0 - control_point_2);
}

double solve_cubic_bezier_parameter(
    const CubicBezierControlPoints& cubic_bezier,
    double alpha) {
    double t = alpha;

    for (int iteration = 0; iteration < 8; ++iteration) {
        const double x = sample_cubic_component(cubic_bezier.cx1, cubic_bezier.cx2, t) - alpha;
        if (std::abs(x) <= 1e-7) {
            return t;
        }

        const double derivative = sample_cubic_derivative(cubic_bezier.cx1, cubic_bezier.cx2, t);
        if (std::abs(derivative) <= 1e-7) {
            break;
        }

        t -= x / derivative;
        t = clamp_unit(t);
    }

    double lower = 0.0;
    double upper = 1.0;
    t = alpha;
    for (int iteration = 0; iteration < 32; ++iteration) {
        const double x = sample_cubic_component(cubic_bezier.cx1, cubic_bezier.cx2, t);
        if (std::abs(x - alpha) <= 1e-7) {
            break;
        }

        if (x < alpha) {
            lower = t;
        } else {
            upper = t;
        }
        t = (lower + upper) * 0.5;
    }

    return t;
}

} // namespace

Interpolation::Interpolation() = default;

Interpolation Interpolation::linear() {
    return Interpolation(InterpolationKind::Linear, {});
}

Interpolation Interpolation::stepped() {
    return Interpolation(InterpolationKind::Stepped, {});
}

Interpolation Interpolation::cubic_bezier(
    double cx1,
    double cy1,
    double cx2,
    double cy2) {
    return Interpolation(
        InterpolationKind::CubicBezier,
        CubicBezierControlPoints{cx1, cy1, cx2, cy2});
}

InterpolationKind Interpolation::kind() const {
    return kind_;
}

const CubicBezierControlPoints& Interpolation::cubic_bezier() const {
    return cubic_bezier_;
}

double Interpolation::transform(double alpha) const {
    alpha = clamp_unit(alpha);

    switch (kind_) {
    case InterpolationKind::Linear:
        return alpha;
    case InterpolationKind::Stepped:
        return alpha >= 1.0 ? 1.0 : 0.0;
    case InterpolationKind::CubicBezier: {
        const double t = solve_cubic_bezier_parameter(cubic_bezier_, alpha);
        return sample_cubic_component(cubic_bezier_.cy1, cubic_bezier_.cy2, t);
    }
    }

    return alpha;
}

double interpolate_value(
    double from_value,
    double to_value,
    const Interpolation& interpolation,
    double alpha) {
    const double eased_alpha = interpolation.transform(alpha);
    return from_value + (to_value - from_value) * eased_alpha;
}

std::optional<double> sample_rotate_timeline(
    const BoneRotateTimeline& timeline,
    double time) {
    if (timeline.keyframes.empty()) {
        return std::nullopt;
    }

    if (timeline.keyframes.size() == 1 || time <= timeline.keyframes.front().time) {
        return timeline.keyframes.front().angle;
    }

    for (std::size_t index = 1; index < timeline.keyframes.size(); ++index) {
        const RotateKeyframe& previous = timeline.keyframes[index - 1];
        const RotateKeyframe& current = timeline.keyframes[index];
        if (time < current.time) {
            const double range = current.time - previous.time;
            const double alpha = range > 0.0 ? (time - previous.time) / range : 0.0;
            return interpolate_value(
                previous.angle,
                current.angle,
                previous.interpolation,
                alpha);
        }
    }

    return timeline.keyframes.back().angle;
}

Interpolation::Interpolation(
    InterpolationKind kind,
    CubicBezierControlPoints cubic_bezier)
    : kind_(kind),
      cubic_bezier_(cubic_bezier) {}

} // namespace marrow::runtime
