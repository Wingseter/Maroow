#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace marrow::runtime {

enum class InterpolationKind {
    Linear,
    Stepped,
    CubicBezier,
};

struct CubicBezierControlPoints {
    double cx1{0.0};
    double cy1{0.0};
    double cx2{1.0};
    double cy2{1.0};
};

class Interpolation {
public:
    Interpolation();

    static Interpolation linear();
    static Interpolation stepped();
    static Interpolation cubic_bezier(
        double cx1,
        double cy1,
        double cx2,
        double cy2);

    InterpolationKind kind() const;
    const CubicBezierControlPoints& cubic_bezier() const;
    double transform(double alpha) const;

private:
    Interpolation(InterpolationKind kind, CubicBezierControlPoints cubic_bezier);

    InterpolationKind kind_{InterpolationKind::Linear};
    CubicBezierControlPoints cubic_bezier_{};
};

struct RotateKeyframe {
    double time{0.0};
    double angle{0.0};
    Interpolation interpolation{};
};

struct BoneRotateTimeline {
    std::size_t bone_index{0};
    std::vector<RotateKeyframe> keyframes;
};

double interpolate_value(
    double from_value,
    double to_value,
    const Interpolation& interpolation,
    double alpha);

std::optional<double> sample_rotate_timeline(
    const BoneRotateTimeline& timeline,
    double time);

} // namespace marrow::runtime
