#include "marrow/runtime/animation.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace marrow::runtime {

struct CubicBezierLookupTable {
    std::array<AnimationScalar, 64U> parameter_by_alpha{};
};

namespace {

constexpr AnimationScalar kTimelineKeyEpsilon = 1e-6f;
constexpr std::size_t kCubicBezierLutSize = 64U;

template <typename T>
T clamp_unit(T value) {
    return std::clamp<T>(value, static_cast<T>(0), static_cast<T>(1));
}

AnimationScalar sample_cubic_component(
    AnimationScalar control_point_1,
    AnimationScalar control_point_2,
    AnimationScalar t) {
    const AnimationScalar inverse_t = 1.0f - t;
    return 3.0f * inverse_t * inverse_t * t * control_point_1 +
           3.0f * inverse_t * t * t * control_point_2 + t * t * t;
}

double sample_cubic_component_precise(double control_point_1, double control_point_2, double t) {
    const double inverse_t = 1.0 - t;
    return 3.0 * inverse_t * inverse_t * t * control_point_1 +
           3.0 * inverse_t * t * t * control_point_2 + t * t * t;
}

double sample_cubic_derivative_precise(
    double control_point_1,
    double control_point_2,
    double t) {
    const double inverse_t = 1.0 - t;
    return 3.0 * inverse_t * inverse_t * control_point_1 +
           6.0 * inverse_t * t * (control_point_2 - control_point_1) +
           3.0 * t * t * (1.0 - control_point_2);
}

double solve_cubic_bezier_parameter_precise(
    const CubicBezierControlPoints& cubic_bezier,
    double alpha) {
    double t = alpha;

    for (int iteration = 0; iteration < 8; ++iteration) {
        const double x = sample_cubic_component_precise(
                             static_cast<double>(cubic_bezier.cx1),
                             static_cast<double>(cubic_bezier.cx2),
                             t) -
            alpha;
        if (std::abs(x) <= 1e-7) {
            return t;
        }

        const double derivative = sample_cubic_derivative_precise(
            static_cast<double>(cubic_bezier.cx1),
            static_cast<double>(cubic_bezier.cx2),
            t);
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
        const double x = sample_cubic_component_precise(
            static_cast<double>(cubic_bezier.cx1),
            static_cast<double>(cubic_bezier.cx2),
            t);
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

struct CubicBezierLookupKey {
    std::uint32_t cx1{0};
    std::uint32_t cy1{0};
    std::uint32_t cx2{0};
    std::uint32_t cy2{0};

    bool operator==(const CubicBezierLookupKey& other) const {
        return cx1 == other.cx1 &&
            cy1 == other.cy1 &&
            cx2 == other.cx2 &&
            cy2 == other.cy2;
    }
};

struct CubicBezierLookupKeyHasher {
    std::size_t operator()(const CubicBezierLookupKey& key) const noexcept {
        std::size_t hash = static_cast<std::size_t>(key.cx1);
        hash ^= static_cast<std::size_t>(key.cy1) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        hash ^= static_cast<std::size_t>(key.cx2) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        hash ^= static_cast<std::size_t>(key.cy2) + 0x9e3779b9U + (hash << 6U) + (hash >> 2U);
        return hash;
    }
};

std::uint32_t float_bits(AnimationScalar value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

CubicBezierLookupKey make_lookup_key(const CubicBezierControlPoints& cubic_bezier) {
    return CubicBezierLookupKey{
        float_bits(cubic_bezier.cx1),
        float_bits(cubic_bezier.cy1),
        float_bits(cubic_bezier.cx2),
        float_bits(cubic_bezier.cy2),
    };
}

const CubicBezierLookupTable* lookup_cubic_bezier_table(
    const CubicBezierControlPoints& cubic_bezier) {
    static std::mutex cache_mutex;
    static std::unordered_map<
        CubicBezierLookupKey,
        std::unique_ptr<CubicBezierLookupTable>,
        CubicBezierLookupKeyHasher>
        cache;

    const CubicBezierLookupKey key = make_lookup_key(cubic_bezier);
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto existing = cache.find(key);
    if (existing != cache.end()) {
        return existing->second.get();
    }

    auto table = std::make_unique<CubicBezierLookupTable>();
    for (std::size_t index = 0; index < kCubicBezierLutSize; ++index) {
        const double alpha = static_cast<double>(index) /
            static_cast<double>(kCubicBezierLutSize - 1U);
        table->parameter_by_alpha[index] =
            static_cast<AnimationScalar>(solve_cubic_bezier_parameter_precise(cubic_bezier, alpha));
    }

    const CubicBezierLookupTable* result = table.get();
    cache.emplace(key, std::move(table));
    return result;
}

AnimationScalar sample_cubic_parameter_lut(
    const CubicBezierLookupTable& lookup_table,
    AnimationScalar alpha) {
    const AnimationScalar scaled =
        clamp_unit(alpha) * static_cast<AnimationScalar>(kCubicBezierLutSize - 1U);
    const std::size_t lower_index = std::min<std::size_t>(
        static_cast<std::size_t>(scaled),
        kCubicBezierLutSize - 1U);
    const std::size_t upper_index = std::min(lower_index + 1U, kCubicBezierLutSize - 1U);
    const AnimationScalar fraction = scaled - static_cast<AnimationScalar>(lower_index);
    const AnimationScalar lower = lookup_table.parameter_by_alpha[lower_index];
    const AnimationScalar upper = lookup_table.parameter_by_alpha[upper_index];
    return lower + (upper - lower) * fraction;
}

std::vector<double> widen_vertex_offsets(const std::vector<AnimationScalar>& vertex_offsets) {
    std::vector<double> widened;
    widened.reserve(vertex_offsets.size());
    for (const AnimationScalar offset : vertex_offsets) {
        widened.push_back(static_cast<double>(offset));
    }
    return widened;
}

template <typename Keyframe>
std::size_t advance_keyframe_cursor(
    const std::vector<Keyframe>& keyframes,
    AnimationScalar time,
    std::size_t cursor,
    AnimationScalar epsilon = 0.0f) {
    if (keyframes.empty()) {
        return 0;
    }

    cursor = std::min(cursor, keyframes.size() - 1);
    while (cursor + 1 < keyframes.size() && time >= keyframes[cursor + 1].time - epsilon) {
        ++cursor;
    }
    while (cursor > 0 && time < keyframes[cursor].time - epsilon) {
        --cursor;
    }

    return cursor;
}

template <typename Keyframe>
std::size_t sample_keyframe_cursor(
    const std::vector<Keyframe>& keyframes,
    AnimationScalar time,
    std::size_t* last_keyframe_index,
    AnimationScalar epsilon = 0.0f) {
    std::size_t cursor = last_keyframe_index != nullptr ? *last_keyframe_index : 0;
    cursor = advance_keyframe_cursor(keyframes, time, cursor, epsilon);
    if (last_keyframe_index != nullptr) {
        *last_keyframe_index = cursor;
    }
    return cursor;
}

} // namespace

void SamplingContext::reset() {
    animation = nullptr;
    last_sample_time = 0.0;
    rotate_last_keyframe_indices.clear();
    inherit_last_keyframe_indices.clear();
    translate_last_keyframe_indices.clear();
    scale_last_keyframe_indices.clear();
    shear_last_keyframe_indices.clear();
    attachment_last_keyframe_indices.clear();
    color_last_keyframe_indices.clear();
    deform_last_keyframe_indices.clear();
    draw_order_last_keyframe_index = 0;
}

Interpolation::Interpolation() = default;

Interpolation Interpolation::linear() {
    return Interpolation(InterpolationKind::Linear, {}, nullptr);
}

Interpolation Interpolation::stepped() {
    return Interpolation(InterpolationKind::Stepped, {}, nullptr);
}

Interpolation Interpolation::cubic_bezier(
    double cx1,
    double cy1,
    double cx2,
    double cy2) {
    return Interpolation(
        InterpolationKind::CubicBezier,
        CubicBezierControlPoints{cx1, cy1, cx2, cy2},
        lookup_cubic_bezier_table(CubicBezierControlPoints{cx1, cy1, cx2, cy2}));
}

InterpolationKind Interpolation::kind() const {
    return kind_;
}

const CubicBezierControlPoints& Interpolation::cubic_bezier() const {
    return cubic_bezier_;
}

AnimationScalar Interpolation::transform_scalar(AnimationScalar alpha) const {
    alpha = clamp_unit(alpha);

    switch (kind_) {
    case InterpolationKind::Linear:
        return alpha;
    case InterpolationKind::Stepped:
        return alpha >= 1.0f ? 1.0f : 0.0f;
    case InterpolationKind::CubicBezier: {
        const AnimationScalar t = cubic_bezier_lut_ != nullptr
            ? sample_cubic_parameter_lut(*cubic_bezier_lut_, alpha)
            : static_cast<AnimationScalar>(solve_cubic_bezier_parameter_precise(
                  cubic_bezier_,
                  static_cast<double>(alpha)));
        return sample_cubic_component(cubic_bezier_.cy1, cubic_bezier_.cy2, t);
    }
    }

    return alpha;
}

double Interpolation::transform(double alpha) const {
    return static_cast<double>(transform_scalar(static_cast<AnimationScalar>(alpha)));
}

AnimationScalar interpolate_value_scalar(
    AnimationScalar from_value,
    AnimationScalar to_value,
    const Interpolation& interpolation,
    AnimationScalar alpha) {
    const AnimationScalar eased_alpha =
        static_cast<AnimationScalar>(interpolation.transform(alpha));
    return from_value + (to_value - from_value) * eased_alpha;
}

double interpolate_value(
    double from_value,
    double to_value,
    const Interpolation& interpolation,
    double alpha) {
    return static_cast<double>(interpolate_value_scalar(
        static_cast<AnimationScalar>(from_value),
        static_cast<AnimationScalar>(to_value),
        interpolation,
        static_cast<AnimationScalar>(alpha)));
}

template <typename Timeline>
std::optional<VectorSample> sample_vector_timeline_impl(
    const Timeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    if (timeline.keyframes.empty()) {
        return std::nullopt;
    }

    const AnimationScalar sample_time = static_cast<AnimationScalar>(time);
    if (timeline.keyframes.size() == 1 || sample_time <= timeline.keyframes.front().time) {
        if (last_keyframe_index != nullptr) {
            *last_keyframe_index = 0;
        }
        const VectorKeyframe& first = timeline.keyframes.front();
        return VectorSample{
            static_cast<double>(first.x),
            static_cast<double>(first.y),
        };
    }

    const std::size_t cursor =
        sample_keyframe_cursor(timeline.keyframes, sample_time, last_keyframe_index);
    if (cursor >= timeline.keyframes.size() - 1) {
        const VectorKeyframe& last = timeline.keyframes.back();
        return VectorSample{
            static_cast<double>(last.x),
            static_cast<double>(last.y),
        };
    }

    const VectorKeyframe& previous = timeline.keyframes[cursor];
    const VectorKeyframe& current = timeline.keyframes[cursor + 1];
    const AnimationScalar range = current.time - previous.time;
    const AnimationScalar alpha =
        range > 0.0f ? (sample_time - previous.time) / range : 0.0f;
        return VectorSample{
        static_cast<double>(interpolate_value_scalar(
            previous.x,
            current.x,
            previous.interpolation,
            alpha)),
        static_cast<double>(interpolate_value_scalar(
            previous.y,
            current.y,
            previous.interpolation,
            alpha)),
    };
}

template <typename Keyframe>
const Keyframe* sample_stepped_keyframe(
    const std::vector<Keyframe>& keyframes,
    double time,
    std::size_t* last_keyframe_index) {
    if (keyframes.empty()) {
        return nullptr;
    }

    const AnimationScalar sample_time = static_cast<AnimationScalar>(time);
    if (keyframes.size() == 1 || sample_time <= keyframes.front().time + kTimelineKeyEpsilon) {
        if (last_keyframe_index != nullptr) {
            *last_keyframe_index = 0;
        }
        return &keyframes.front();
    }

    const std::size_t cursor =
        sample_keyframe_cursor(
            keyframes,
            sample_time,
            last_keyframe_index,
            kTimelineKeyEpsilon);
    return &keyframes[cursor];
}

std::optional<double> sample_rotate_timeline(
    const BoneRotateTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    if (timeline.keyframes.empty()) {
        return std::nullopt;
    }

    const AnimationScalar sample_time = static_cast<AnimationScalar>(time);
    if (timeline.keyframes.size() == 1 || sample_time <= timeline.keyframes.front().time) {
        if (last_keyframe_index != nullptr) {
            *last_keyframe_index = 0;
        }
        return static_cast<double>(timeline.setup_rotation + timeline.keyframes.front().angle);
    }

    const std::size_t cursor =
        sample_keyframe_cursor(timeline.keyframes, sample_time, last_keyframe_index);
    if (cursor >= timeline.keyframes.size() - 1) {
        return static_cast<double>(timeline.setup_rotation + timeline.keyframes.back().angle);
    }

    const RotateKeyframe& previous = timeline.keyframes[cursor];
    const RotateKeyframe& current = timeline.keyframes[cursor + 1];
    const AnimationScalar range = current.time - previous.time;
    const AnimationScalar alpha =
        range > 0.0f ? (sample_time - previous.time) / range : 0.0f;
    return static_cast<double>(timeline.setup_rotation + interpolate_value_scalar(
        previous.angle,
        current.angle,
        previous.interpolation,
        alpha));
}

const InheritKeyframe* sample_inherit_timeline(
    const BoneInheritTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    return sample_stepped_keyframe(timeline.keyframes, time, last_keyframe_index);
}

std::optional<VectorSample> sample_translate_timeline(
    const BoneTranslateTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    return sample_vector_timeline_impl(timeline, time, last_keyframe_index);
}

std::optional<VectorSample> sample_scale_timeline(
    const BoneScaleTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    return sample_vector_timeline_impl(timeline, time, last_keyframe_index);
}

std::optional<VectorSample> sample_shear_timeline(
    const BoneShearTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    return sample_vector_timeline_impl(timeline, time, last_keyframe_index);
}

const AttachmentKeyframe* sample_attachment_timeline(
    const SlotAttachmentTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    return sample_stepped_keyframe(timeline.keyframes, time, last_keyframe_index);
}

std::optional<SlotColor> sample_color_timeline(
    const SlotColorTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    if (timeline.keyframes.empty()) {
        return std::nullopt;
    }

    const AnimationScalar sample_time = static_cast<AnimationScalar>(time);
    if (timeline.keyframes.size() == 1 || sample_time <= timeline.keyframes.front().time) {
        if (last_keyframe_index != nullptr) {
            *last_keyframe_index = 0;
        }
        return timeline.keyframes.front().color;
    }

    const std::size_t cursor =
        sample_keyframe_cursor(timeline.keyframes, sample_time, last_keyframe_index);
    if (cursor >= timeline.keyframes.size() - 1) {
        return timeline.keyframes.back().color;
    }

    const ColorKeyframe& previous = timeline.keyframes[cursor];
    const ColorKeyframe& current = timeline.keyframes[cursor + 1];
    const AnimationScalar range = current.time - previous.time;
    const AnimationScalar alpha =
        range > 0.0f ? (sample_time - previous.time) / range : 0.0f;
    return SlotColor{
        interpolate_value_scalar(previous.color.r, current.color.r, previous.interpolation, alpha),
        interpolate_value_scalar(previous.color.g, current.color.g, previous.interpolation, alpha),
        interpolate_value_scalar(previous.color.b, current.color.b, previous.interpolation, alpha),
        interpolate_value_scalar(previous.color.a, current.color.a, previous.interpolation, alpha)};
}

std::optional<std::vector<double>> sample_deform_timeline(
    const MeshDeformTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    if (timeline.keyframes.empty()) {
        return std::nullopt;
    }

    const AnimationScalar sample_time = static_cast<AnimationScalar>(time);
    if (timeline.keyframes.size() == 1 || sample_time <= timeline.keyframes.front().time) {
        if (last_keyframe_index != nullptr) {
            *last_keyframe_index = 0;
        }
        return widen_vertex_offsets(timeline.keyframes.front().vertex_offsets);
    }

    const std::size_t cursor =
        sample_keyframe_cursor(timeline.keyframes, sample_time, last_keyframe_index);
    if (cursor >= timeline.keyframes.size() - 1) {
        return widen_vertex_offsets(timeline.keyframes.back().vertex_offsets);
    }

    const DeformKeyframe& previous = timeline.keyframes[cursor];
    const DeformKeyframe& current = timeline.keyframes[cursor + 1];
    if (previous.vertex_offsets.size() != current.vertex_offsets.size()) {
        return std::nullopt;
    }

    const AnimationScalar range = current.time - previous.time;
    const AnimationScalar alpha =
        range > 0.0f ? (sample_time - previous.time) / range : 0.0f;
    std::vector<double> vertex_offsets;
    vertex_offsets.reserve(previous.vertex_offsets.size());
    for (std::size_t component_index = 0;
         component_index < previous.vertex_offsets.size();
         ++component_index) {
        vertex_offsets.push_back(static_cast<double>(interpolate_value_scalar(
            previous.vertex_offsets[component_index],
            current.vertex_offsets[component_index],
            previous.interpolation,
            alpha)));
    }
    return vertex_offsets;
}

const DrawOrderKeyframe* sample_draw_order_timeline(
    const DrawOrderTimeline& timeline,
    double time,
    std::size_t* last_keyframe_index) {
    return sample_stepped_keyframe(timeline.keyframes, time, last_keyframe_index);
}

Interpolation::Interpolation(
    InterpolationKind kind,
    CubicBezierControlPoints cubic_bezier,
    const CubicBezierLookupTable* cubic_bezier_lut)
    : kind_(kind),
      cubic_bezier_(cubic_bezier),
      cubic_bezier_lut_(cubic_bezier_lut) {}

} // namespace marrow::runtime
