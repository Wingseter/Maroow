#include "skeleton_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace marrow::runtime {
namespace {

constexpr char kBinaryMagic[] = {'M', 'B', 'I', 'N'};
constexpr char kAnimationSectionMagic[] = {'A', 'K', 'E', 'Y'};
constexpr std::uint64_t kBinaryVersionGenericDocument = 1;
constexpr std::uint64_t kBinaryVersionPackedAnimations = 2;
constexpr std::size_t kMaxDecodeDepth = 1024;
constexpr double kU16Range = 65535.0;
constexpr double kRotationMinDegrees = -180.0;
constexpr double kRotationRangeDegrees = 360.0;
constexpr double kFixedBezierCx1 = 1.0 / 3.0;
constexpr double kFixedBezierCx2 = 2.0 / 3.0;
constexpr std::size_t kValidationSubdivisionsPerSegment = 4;

enum class NodeTag : std::uint8_t {
    Null = 0,
    Boolean = 1,
    Number = 2,
    String = 3,
    Array = 4,
    Object = 5,
};

enum class PackedChannelKind : std::uint8_t {
    Rotate = 0,
    Translate = 1,
};

struct PackedInterpolationDescriptor {
    InterpolationKind kind{InterpolationKind::Linear};
    CubicBezierControlPoints cubic{};
};

struct PackedAnimationChannel {
    std::size_t bone_index{0};
    PackedChannelKind kind{PackedChannelKind::Rotate};
    std::vector<std::uint16_t> times;
    std::vector<std::uint32_t> payloads;
    std::vector<double> rotate_values;
    std::vector<VectorSample> translate_values;
    std::vector<PackedInterpolationDescriptor> interpolations;
};

struct PackedAnimationPayload {
    std::string name;
    double duration{0.0};
    double min_translate_x{0.0};
    double min_translate_y{0.0};
    double max_translate_x{0.0};
    double max_translate_y{0.0};
    std::vector<PackedAnimationChannel> channels;
    bool keyframes_sorted_by_time_and_bone{true};
};

struct CombinedPackedKey {
    std::uint16_t time{0};
    std::uint16_t channel_index{0};
    std::uint32_t payload{0};
    std::size_t bone_index{0};
    PackedChannelKind kind{PackedChannelKind::Rotate};
};

struct AnimationTranslateBounds {
    bool has_value{false};
    double min_x{0.0};
    double min_y{0.0};
    double max_x{0.0};
    double max_y{0.0};
};

template <typename Keyframe>
struct ReducedKeyLayout {
    std::vector<std::size_t> indices;
    std::vector<PackedInterpolationDescriptor> interpolations;
};

json::LoadResult make_failure(const std::filesystem::path& path, std::string message) {
    json::LoadResult result;
    result.error = json::LoadError{
        path,
        {},
        std::move(message),
        {},
        {},
    };
    return result;
}

std::optional<json::LoadError> make_write_failure(
    const std::filesystem::path& path,
    std::string message) {
    return json::LoadError{
        path,
        {},
        std::move(message),
        {},
        {},
    };
}

void append_varint(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0) {
            byte |= 0x80U;
        }
        bytes.push_back(byte);
    } while (value != 0);
}

void append_uint16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
}

void append_uint32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24U) & 0xFFU));
}

void append_float32(std::vector<std::uint8_t>& bytes, double value) {
    const float narrowed = static_cast<float>(value);
    std::uint32_t raw = 0;
    std::memcpy(&raw, &narrowed, sizeof(raw));
    append_uint32(bytes, raw);
}

struct StringTableBuilder {
    std::vector<std::string> values;
    std::unordered_map<std::string, std::uint64_t> indices;

    std::uint64_t intern(std::string_view value) {
        const auto iterator = indices.find(std::string(value));
        if (iterator != indices.end()) {
            return iterator->second;
        }

        const std::uint64_t index = static_cast<std::uint64_t>(values.size());
        values.emplace_back(value);
        indices.emplace(values.back(), index);
        return index;
    }
};

void collect_strings(const json::Value& value, StringTableBuilder& table) {
    switch (value.type()) {
    case json::Value::Type::String:
        table.intern(value.as_string());
        return;
    case json::Value::Type::Array:
        for (const json::Value& element : value.as_array()) {
            collect_strings(element, table);
        }
        return;
    case json::Value::Type::Object:
        for (const auto& [key, member] : value.as_object()) {
            table.intern(key);
            collect_strings(member, table);
        }
        return;
    case json::Value::Type::Null:
    case json::Value::Type::Boolean:
    case json::Value::Type::Number:
        return;
    }
}

void collect_booleans(const json::Value& value, std::vector<std::uint8_t>& values) {
    switch (value.type()) {
    case json::Value::Type::Boolean:
        values.push_back(value.as_boolean() ? 1U : 0U);
        return;
    case json::Value::Type::Array:
        for (const json::Value& element : value.as_array()) {
            collect_booleans(element, values);
        }
        return;
    case json::Value::Type::Object:
        for (const auto& [key, member] : value.as_object()) {
            static_cast<void>(key);
            collect_booleans(member, values);
        }
        return;
    case json::Value::Type::Null:
    case json::Value::Type::Number:
    case json::Value::Type::String:
        return;
    }
}

std::vector<std::uint8_t> pack_booleans(const std::vector<std::uint8_t>& values) {
    std::vector<std::uint8_t> packed((values.size() + 7U) / 8U, 0U);
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (values[index] == 0U) {
            continue;
        }

        packed[index / 8U] |= static_cast<std::uint8_t>(1U << (index % 8U));
    }
    return packed;
}

void encode_value(
    const json::Value& value,
    const StringTableBuilder& strings,
    std::vector<std::uint8_t>& bytes) {
    switch (value.type()) {
    case json::Value::Type::Null:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Null));
        return;
    case json::Value::Type::Boolean:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Boolean));
        return;
    case json::Value::Type::Number:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Number));
        append_float32(bytes, value.as_number());
        return;
    case json::Value::Type::String:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::String));
        append_varint(bytes, strings.indices.at(value.as_string()));
        return;
    case json::Value::Type::Array:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Array));
        append_varint(bytes, static_cast<std::uint64_t>(value.as_array().size()));
        for (const json::Value& element : value.as_array()) {
            encode_value(element, strings, bytes);
        }
        return;
    case json::Value::Type::Object:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Object));
        append_varint(bytes, static_cast<std::uint64_t>(value.as_object().size()));
        for (const auto& [key, member] : value.as_object()) {
            append_varint(bytes, strings.indices.at(key));
            encode_value(member, strings, bytes);
        }
        return;
    }
}

double clamp_unit(double value) {
    return std::clamp(value, 0.0, 1.0);
}

std::uint16_t quantize_unit_interval(double value) {
    const double scaled = std::round(clamp_unit(value) * kU16Range);
    return static_cast<std::uint16_t>(std::clamp(scaled, 0.0, kU16Range));
}

double dequantize_unit_interval(std::uint16_t value) {
    return static_cast<double>(value) / kU16Range;
}

std::uint16_t quantize_rotation_degrees(double angle_degrees) {
    const double normalized = detail::normalize_rotation_degrees(angle_degrees);
    return quantize_unit_interval((normalized - kRotationMinDegrees) / kRotationRangeDegrees);
}

double decode_rotation_degrees(std::uint16_t encoded_angle) {
    return kRotationMinDegrees + dequantize_unit_interval(encoded_angle) * kRotationRangeDegrees;
}

std::uint16_t quantize_time_value(double time, double duration) {
    if (duration <= 0.0) {
        return 0;
    }
    return quantize_unit_interval(time / duration);
}

double decode_time_value(std::uint16_t encoded_time, double duration) {
    if (duration <= 0.0) {
        return 0.0;
    }
    return dequantize_unit_interval(encoded_time) * duration;
}

std::uint16_t quantize_translate_component(
    double value,
    double min_value,
    double max_value) {
    if (max_value - min_value <= 1e-9) {
        return 0;
    }
    return quantize_unit_interval((value - min_value) / (max_value - min_value));
}

double decode_translate_component(
    std::uint16_t encoded,
    double min_value,
    double max_value) {
    if (max_value - min_value <= 1e-9) {
        return min_value;
    }
    return min_value + (max_value - min_value) * dequantize_unit_interval(encoded);
}

std::uint32_t pack_translate_payload(std::uint16_t x, std::uint16_t y) {
    return static_cast<std::uint32_t>(x) |
        (static_cast<std::uint32_t>(y) << 16U);
}

VectorSample decode_translate_payload(
    std::uint32_t payload,
    const PackedAnimationPayload& animation_payload) {
    const std::uint16_t x_encoded = static_cast<std::uint16_t>(payload & 0xFFFFU);
    const std::uint16_t y_encoded = static_cast<std::uint16_t>((payload >> 16U) & 0xFFFFU);
    return {
        decode_translate_component(
            x_encoded,
            animation_payload.min_translate_x,
            animation_payload.max_translate_x),
        decode_translate_component(
            y_encoded,
            animation_payload.min_translate_y,
            animation_payload.max_translate_y)};
}

PackedInterpolationDescriptor pack_interpolation_descriptor(const Interpolation& interpolation) {
    PackedInterpolationDescriptor descriptor;
    descriptor.kind = interpolation.kind();
    if (descriptor.kind == InterpolationKind::CubicBezier) {
        descriptor.cubic = interpolation.cubic_bezier();
    }
    return descriptor;
}

Interpolation unpack_interpolation_descriptor(const PackedInterpolationDescriptor& descriptor) {
    switch (descriptor.kind) {
    case InterpolationKind::Linear:
        return Interpolation::linear();
    case InterpolationKind::Stepped:
        return Interpolation::stepped();
    case InterpolationKind::CubicBezier:
        return Interpolation::cubic_bezier(
            descriptor.cubic.cx1,
            descriptor.cubic.cy1,
            descriptor.cubic.cx2,
            descriptor.cubic.cy2);
    }

    return Interpolation::linear();
}

template <typename Keyframe>
std::vector<double> build_validation_sample_times(
    const std::vector<Keyframe>& keyframes,
    std::size_t start_index,
    std::size_t end_index) {
    std::vector<double> sample_times;
    sample_times.reserve((end_index - start_index + 1U) * (kValidationSubdivisionsPerSegment + 1U));
    for (std::size_t index = start_index; index < end_index; ++index) {
        const double start_time = keyframes[index].time;
        const double end_time = keyframes[index + 1U].time;
        sample_times.push_back(start_time);
        const double span = end_time - start_time;
        if (span > 0.0) {
            for (std::size_t subdivision = 1;
                 subdivision < kValidationSubdivisionsPerSegment;
                 ++subdivision) {
                sample_times.push_back(
                    start_time + span * static_cast<double>(subdivision) /
                        static_cast<double>(kValidationSubdivisionsPerSegment));
            }
        }
    }
    sample_times.push_back(keyframes[end_index].time);
    std::sort(sample_times.begin(), sample_times.end());
    sample_times.erase(
        std::unique(
            sample_times.begin(),
            sample_times.end(),
            [](double left, double right) {
                return std::abs(left - right) <= 1e-6;
            }),
        sample_times.end());
    return sample_times;
}

std::optional<double> sample_relative_rotate_timeline(
    const BoneRotateTimeline& timeline,
    double time) {
    if (timeline.keyframes.empty()) {
        return std::nullopt;
    }

    if (timeline.keyframes.size() == 1 || time <= timeline.keyframes.front().time) {
        return timeline.keyframes.front().angle;
    }

    for (std::size_t index = 1; index < timeline.keyframes.size(); ++index) {
        const RotateKeyframe& previous = timeline.keyframes[index - 1U];
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

std::optional<double> sample_packed_rotate_channel(
    const PackedAnimationChannel& channel,
    double time) {
    if (channel.times.empty()) {
        return std::nullopt;
    }

    if (channel.times.size() == 1 || time <= decode_time_value(channel.times.front(), 1.0)) {
        return channel.rotate_values.front();
    }

    for (std::size_t index = 1; index < channel.times.size(); ++index) {
        const double previous_time = decode_time_value(channel.times[index - 1U], 1.0);
        const double current_time = decode_time_value(channel.times[index], 1.0);
        if (time < current_time) {
            const double range = current_time - previous_time;
            const double alpha = range > 0.0 ? (time - previous_time) / range : 0.0;
            return interpolate_value(
                channel.rotate_values[index - 1U],
                channel.rotate_values[index],
                unpack_interpolation_descriptor(channel.interpolations[index - 1U]),
                alpha);
        }
    }

    return channel.rotate_values.back();
}

std::optional<double> sample_packed_rotate_channel(
    const PackedAnimationChannel& channel,
    double time,
    double duration) {
    if (channel.times.empty()) {
        return std::nullopt;
    }

    if (channel.times.size() == 1 || time <= decode_time_value(channel.times.front(), duration)) {
        return channel.rotate_values.front();
    }

    for (std::size_t index = 1; index < channel.times.size(); ++index) {
        const double previous_time = decode_time_value(channel.times[index - 1U], duration);
        const double current_time = decode_time_value(channel.times[index], duration);
        if (time < current_time) {
            const double range = current_time - previous_time;
            const double alpha = range > 0.0 ? (time - previous_time) / range : 0.0;
            return interpolate_value(
                channel.rotate_values[index - 1U],
                channel.rotate_values[index],
                unpack_interpolation_descriptor(channel.interpolations[index - 1U]),
                alpha);
        }
    }

    return channel.rotate_values.back();
}

std::optional<VectorSample> sample_packed_translate_channel(
    const PackedAnimationChannel& channel,
    double time,
    double duration) {
    if (channel.times.empty()) {
        return std::nullopt;
    }

    if (channel.times.size() == 1 || time <= decode_time_value(channel.times.front(), duration)) {
        return channel.translate_values.front();
    }

    for (std::size_t index = 1; index < channel.times.size(); ++index) {
        const double previous_time = decode_time_value(channel.times[index - 1U], duration);
        const double current_time = decode_time_value(channel.times[index], duration);
        if (time < current_time) {
            const double range = current_time - previous_time;
            const double alpha = range > 0.0 ? (time - previous_time) / range : 0.0;
            const Interpolation interpolation =
                unpack_interpolation_descriptor(channel.interpolations[index - 1U]);
            return VectorSample{
                interpolate_value(
                    channel.translate_values[index - 1U].x,
                    channel.translate_values[index].x,
                    interpolation,
                    alpha),
                interpolate_value(
                    channel.translate_values[index - 1U].y,
                    channel.translate_values[index].y,
                    interpolation,
                    alpha)};
        }
    }

    return channel.translate_values.back();
}

bool validate_rotate_segment_candidate(
    const BoneRotateTimeline& original_timeline,
    std::size_t start_index,
    std::size_t end_index,
    double duration,
    const PackedInterpolationDescriptor& interpolation,
    double tolerance_degrees) {
    const std::uint16_t start_time = quantize_time_value(
        original_timeline.keyframes[start_index].time,
        duration);
    const std::uint16_t end_time = quantize_time_value(
        original_timeline.keyframes[end_index].time,
        duration);
    if (end_time <= start_time) {
        return false;
    }

    PackedAnimationChannel channel;
    channel.kind = PackedChannelKind::Rotate;
    channel.times = {start_time, end_time};
    channel.rotate_values = {
        decode_rotation_degrees(
            quantize_rotation_degrees(original_timeline.keyframes[start_index].angle)),
        decode_rotation_degrees(
            quantize_rotation_degrees(original_timeline.keyframes[end_index].angle)),
    };
    channel.interpolations = {interpolation};

    const std::vector<double> sample_times = build_validation_sample_times(
        original_timeline.keyframes,
        start_index,
        end_index);
    for (const double sample_time : sample_times) {
        const std::optional<double> original_value =
            sample_relative_rotate_timeline(original_timeline, sample_time);
        const std::optional<double> candidate_value =
            sample_packed_rotate_channel(channel, sample_time, duration);
        if (!original_value.has_value() || !candidate_value.has_value()) {
            return false;
        }

        if (std::abs(detail::normalize_rotation_degrees(
                *candidate_value - *original_value)) > tolerance_degrees) {
            return false;
        }
    }

    return true;
}

bool validate_translate_segment_candidate(
    const BoneTranslateTimeline& original_timeline,
    std::size_t start_index,
    std::size_t end_index,
    double duration,
    const AnimationTranslateBounds& bounds,
    const PackedInterpolationDescriptor& interpolation,
    double tolerance_pixels) {
    const std::uint16_t start_time = quantize_time_value(
        original_timeline.keyframes[start_index].time,
        duration);
    const std::uint16_t end_time = quantize_time_value(
        original_timeline.keyframes[end_index].time,
        duration);
    if (end_time <= start_time) {
        return false;
    }

    const VectorKeyframe& start_key = original_timeline.keyframes[start_index];
    const VectorKeyframe& end_key = original_timeline.keyframes[end_index];
    const std::uint16_t start_x = quantize_translate_component(
        start_key.x,
        bounds.min_x,
        bounds.max_x);
    const std::uint16_t start_y = quantize_translate_component(
        start_key.y,
        bounds.min_y,
        bounds.max_y);
    const std::uint16_t end_x = quantize_translate_component(
        end_key.x,
        bounds.min_x,
        bounds.max_x);
    const std::uint16_t end_y = quantize_translate_component(
        end_key.y,
        bounds.min_y,
        bounds.max_y);

    PackedAnimationChannel channel;
    channel.kind = PackedChannelKind::Translate;
    channel.times = {start_time, end_time};
    channel.translate_values = {
        {
            decode_translate_component(start_x, bounds.min_x, bounds.max_x),
            decode_translate_component(start_y, bounds.min_y, bounds.max_y),
        },
        {
            decode_translate_component(end_x, bounds.min_x, bounds.max_x),
            decode_translate_component(end_y, bounds.min_y, bounds.max_y),
        },
    };
    channel.interpolations = {interpolation};

    const std::vector<double> sample_times = build_validation_sample_times(
        original_timeline.keyframes,
        start_index,
        end_index);
    for (const double sample_time : sample_times) {
        const std::optional<VectorSample> original_value =
            sample_translate_timeline(original_timeline, sample_time);
        const std::optional<VectorSample> candidate_value =
            sample_packed_translate_channel(channel, sample_time, duration);
        if (!original_value.has_value() || !candidate_value.has_value()) {
            return false;
        }

        if (std::hypot(
                candidate_value->x - original_value->x,
                candidate_value->y - original_value->y) > tolerance_pixels) {
            return false;
        }
    }

    return true;
}

std::optional<PackedInterpolationDescriptor> fit_rotation_segment_interpolation(
    const BoneRotateTimeline& original_timeline,
    std::size_t start_index,
    std::size_t end_index,
    double duration,
    double tolerance_degrees) {
    PackedInterpolationDescriptor stepped;
    stepped.kind = InterpolationKind::Stepped;
    if (validate_rotate_segment_candidate(
            original_timeline,
            start_index,
            end_index,
            duration,
            stepped,
            tolerance_degrees)) {
        return stepped;
    }

    PackedInterpolationDescriptor linear;
    linear.kind = InterpolationKind::Linear;
    if (validate_rotate_segment_candidate(
            original_timeline,
            start_index,
            end_index,
            duration,
            linear,
            tolerance_degrees)) {
        return linear;
    }

    const RotateKeyframe& start_key = original_timeline.keyframes[start_index];
    const RotateKeyframe& end_key = original_timeline.keyframes[end_index];
    const double span = end_key.time - start_key.time;
    const double delta = end_key.angle - start_key.angle;
    if (span <= 0.0 || std::abs(delta) <= 1e-8) {
        return std::nullopt;
    }

    double s11 = 0.0;
    double s12 = 0.0;
    double s22 = 0.0;
    double r1 = 0.0;
    double r2 = 0.0;
    std::size_t sample_count = 0;

    const std::vector<double> sample_times = build_validation_sample_times(
        original_timeline.keyframes,
        start_index,
        end_index);
    for (const double sample_time : sample_times) {
        if (sample_time <= start_key.time + 1e-8 || sample_time >= end_key.time - 1e-8) {
            continue;
        }

        const std::optional<double> sample_value =
            sample_relative_rotate_timeline(original_timeline, sample_time);
        if (!sample_value.has_value()) {
            continue;
        }

        const double x = (sample_time - start_key.time) / span;
        const double inverse_x = 1.0 - x;
        const double b1 = 3.0 * inverse_x * inverse_x * x;
        const double b2 = 3.0 * inverse_x * x * x;
        const double rhs = ((*sample_value - start_key.angle) / delta) - (x * x * x);
        s11 += b1 * b1;
        s12 += b1 * b2;
        s22 += b2 * b2;
        r1 += b1 * rhs;
        r2 += b2 * rhs;
        ++sample_count;
    }

    if (sample_count == 0) {
        return std::nullopt;
    }

    const double determinant = s11 * s22 - s12 * s12;
    if (std::abs(determinant) <= 1e-10) {
        return std::nullopt;
    }

    PackedInterpolationDescriptor cubic;
    cubic.kind = InterpolationKind::CubicBezier;
    cubic.cubic.cx1 = kFixedBezierCx1;
    cubic.cubic.cx2 = kFixedBezierCx2;
    cubic.cubic.cy1 = (r1 * s22 - r2 * s12) / determinant;
    cubic.cubic.cy2 = (s11 * r2 - s12 * r1) / determinant;
    if (validate_rotate_segment_candidate(
            original_timeline,
            start_index,
            end_index,
            duration,
            cubic,
            tolerance_degrees)) {
        return cubic;
    }

    return std::nullopt;
}

std::optional<PackedInterpolationDescriptor> fit_translate_segment_interpolation(
    const BoneTranslateTimeline& original_timeline,
    std::size_t start_index,
    std::size_t end_index,
    double duration,
    const AnimationTranslateBounds& bounds,
    double tolerance_pixels) {
    PackedInterpolationDescriptor stepped;
    stepped.kind = InterpolationKind::Stepped;
    if (validate_translate_segment_candidate(
            original_timeline,
            start_index,
            end_index,
            duration,
            bounds,
            stepped,
            tolerance_pixels)) {
        return stepped;
    }

    PackedInterpolationDescriptor linear;
    linear.kind = InterpolationKind::Linear;
    if (validate_translate_segment_candidate(
            original_timeline,
            start_index,
            end_index,
            duration,
            bounds,
            linear,
            tolerance_pixels)) {
        return linear;
    }

    const VectorKeyframe& start_key = original_timeline.keyframes[start_index];
    const VectorKeyframe& end_key = original_timeline.keyframes[end_index];
    const double span = end_key.time - start_key.time;
    const double delta_x = end_key.x - start_key.x;
    const double delta_y = end_key.y - start_key.y;
    const double delta_length_squared = delta_x * delta_x + delta_y * delta_y;
    if (span <= 0.0 || delta_length_squared <= 1e-8) {
        return std::nullopt;
    }

    double s11 = 0.0;
    double s12 = 0.0;
    double s22 = 0.0;
    double r1 = 0.0;
    double r2 = 0.0;
    std::size_t sample_count = 0;

    const std::vector<double> sample_times = build_validation_sample_times(
        original_timeline.keyframes,
        start_index,
        end_index);
    for (const double sample_time : sample_times) {
        if (sample_time <= start_key.time + 1e-8 || sample_time >= end_key.time - 1e-8) {
            continue;
        }

        const std::optional<VectorSample> sample_value =
            sample_translate_timeline(original_timeline, sample_time);
        if (!sample_value.has_value()) {
            continue;
        }

        const double x = (sample_time - start_key.time) / span;
        const double inverse_x = 1.0 - x;
        const double b1 = 3.0 * inverse_x * inverse_x * x;
        const double b2 = 3.0 * inverse_x * x * x;
        const double projected = ((sample_value->x - start_key.x) * delta_x +
                                  (sample_value->y - start_key.y) * delta_y) /
            delta_length_squared;
        const double rhs = projected - (x * x * x);
        s11 += b1 * b1;
        s12 += b1 * b2;
        s22 += b2 * b2;
        r1 += b1 * rhs;
        r2 += b2 * rhs;
        ++sample_count;
    }

    if (sample_count == 0) {
        return std::nullopt;
    }

    const double determinant = s11 * s22 - s12 * s12;
    if (std::abs(determinant) <= 1e-10) {
        return std::nullopt;
    }

    PackedInterpolationDescriptor cubic;
    cubic.kind = InterpolationKind::CubicBezier;
    cubic.cubic.cx1 = kFixedBezierCx1;
    cubic.cubic.cx2 = kFixedBezierCx2;
    cubic.cubic.cy1 = (r1 * s22 - r2 * s12) / determinant;
    cubic.cubic.cy2 = (s11 * r2 - s12 * r1) / determinant;
    if (validate_translate_segment_candidate(
            original_timeline,
            start_index,
            end_index,
            duration,
            bounds,
            cubic,
            tolerance_pixels)) {
        return cubic;
    }

    return std::nullopt;
}

ReducedKeyLayout<RotateKeyframe> reduce_rotate_timeline_layout(
    const BoneRotateTimeline& timeline,
    double duration,
    double tolerance_degrees) {
    ReducedKeyLayout<RotateKeyframe> layout;
    layout.indices.reserve(timeline.keyframes.size());
    layout.interpolations.reserve(timeline.keyframes.size());

    for (std::size_t index = 0; index < timeline.keyframes.size(); ++index) {
        layout.indices.push_back(index);
        layout.interpolations.push_back(
            pack_interpolation_descriptor(timeline.keyframes[index].interpolation));
    }

    bool removed_any = true;
    while (removed_any && layout.indices.size() > 2U) {
        removed_any = false;
        for (std::size_t current = 1; current + 1U < layout.indices.size(); ++current) {
            const std::size_t start_index = layout.indices[current - 1U];
            const std::size_t end_index = layout.indices[current + 1U];
            const std::optional<PackedInterpolationDescriptor> candidate =
                fit_rotation_segment_interpolation(
                    timeline,
                    start_index,
                    end_index,
                    duration,
                    tolerance_degrees);
            if (!candidate.has_value()) {
                continue;
            }

            layout.interpolations[current - 1U] = *candidate;
            layout.indices.erase(layout.indices.begin() + static_cast<std::ptrdiff_t>(current));
            layout.interpolations.erase(
                layout.interpolations.begin() + static_cast<std::ptrdiff_t>(current));
            removed_any = true;
            break;
        }
    }

    return layout;
}

ReducedKeyLayout<VectorKeyframe> reduce_translate_timeline_layout(
    const BoneTranslateTimeline& timeline,
    double duration,
    const AnimationTranslateBounds& bounds,
    double tolerance_pixels) {
    ReducedKeyLayout<VectorKeyframe> layout;
    layout.indices.reserve(timeline.keyframes.size());
    layout.interpolations.reserve(timeline.keyframes.size());

    for (std::size_t index = 0; index < timeline.keyframes.size(); ++index) {
        layout.indices.push_back(index);
        layout.interpolations.push_back(
            pack_interpolation_descriptor(timeline.keyframes[index].interpolation));
    }

    bool removed_any = true;
    while (removed_any && layout.indices.size() > 2U) {
        removed_any = false;
        for (std::size_t current = 1; current + 1U < layout.indices.size(); ++current) {
            const std::size_t start_index = layout.indices[current - 1U];
            const std::size_t end_index = layout.indices[current + 1U];
            const std::optional<PackedInterpolationDescriptor> candidate =
                fit_translate_segment_interpolation(
                    timeline,
                    start_index,
                    end_index,
                    duration,
                    bounds,
                    tolerance_pixels);
            if (!candidate.has_value()) {
                continue;
            }

            layout.interpolations[current - 1U] = *candidate;
            layout.indices.erase(layout.indices.begin() + static_cast<std::ptrdiff_t>(current));
            layout.interpolations.erase(
                layout.interpolations.begin() + static_cast<std::ptrdiff_t>(current));
            removed_any = true;
            break;
        }
    }

    return layout;
}

bool populate_rotate_channel(
    const BoneRotateTimeline& original_timeline,
    const ReducedKeyLayout<RotateKeyframe>& reduced_layout,
    double duration,
    double tolerance_degrees,
    PackedAnimationChannel* channel_out) {
    PackedAnimationChannel channel;
    channel.bone_index = original_timeline.bone_index;
    channel.kind = PackedChannelKind::Rotate;

    for (std::size_t reduced_index = 0; reduced_index < reduced_layout.indices.size(); ++reduced_index) {
        const RotateKeyframe& keyframe =
            original_timeline.keyframes[reduced_layout.indices[reduced_index]];
        const std::uint16_t encoded_time = quantize_time_value(keyframe.time, duration);
        if (!channel.times.empty() && encoded_time <= channel.times.back()) {
            return false;
        }

        const std::uint16_t encoded_angle = quantize_rotation_degrees(keyframe.angle);
        channel.times.push_back(encoded_time);
        channel.payloads.push_back(static_cast<std::uint32_t>(encoded_angle));
        channel.rotate_values.push_back(decode_rotation_degrees(encoded_angle));
        if (reduced_index + 1U < reduced_layout.indices.size()) {
            channel.interpolations.push_back(reduced_layout.interpolations[reduced_index]);
        }
    }

    const std::vector<double> sample_times = build_validation_sample_times(
        original_timeline.keyframes,
        0,
        original_timeline.keyframes.size() - 1U);
    for (const double sample_time : sample_times) {
        const std::optional<double> original_value =
            sample_relative_rotate_timeline(original_timeline, sample_time);
        const std::optional<double> roundtrip_value =
            sample_packed_rotate_channel(channel, sample_time, duration);
        if (!original_value.has_value() || !roundtrip_value.has_value()) {
            return false;
        }

        if (std::abs(detail::normalize_rotation_degrees(
                *roundtrip_value - *original_value)) > tolerance_degrees) {
            return false;
        }
    }

    *channel_out = std::move(channel);
    return true;
}

bool populate_translate_channel(
    const BoneTranslateTimeline& original_timeline,
    const ReducedKeyLayout<VectorKeyframe>& reduced_layout,
    double duration,
    const AnimationTranslateBounds& bounds,
    double tolerance_pixels,
    PackedAnimationChannel* channel_out) {
    PackedAnimationChannel channel;
    channel.bone_index = original_timeline.bone_index;
    channel.kind = PackedChannelKind::Translate;

    for (std::size_t reduced_index = 0; reduced_index < reduced_layout.indices.size(); ++reduced_index) {
        const VectorKeyframe& keyframe =
            original_timeline.keyframes[reduced_layout.indices[reduced_index]];
        const std::uint16_t encoded_time = quantize_time_value(keyframe.time, duration);
        if (!channel.times.empty() && encoded_time <= channel.times.back()) {
            return false;
        }

        const std::uint16_t encoded_x = quantize_translate_component(
            keyframe.x,
            bounds.min_x,
            bounds.max_x);
        const std::uint16_t encoded_y = quantize_translate_component(
            keyframe.y,
            bounds.min_y,
            bounds.max_y);
        channel.times.push_back(encoded_time);
        channel.payloads.push_back(pack_translate_payload(encoded_x, encoded_y));
        channel.translate_values.push_back(
            decode_translate_payload(channel.payloads.back(), PackedAnimationPayload{
                {},
                duration,
                bounds.min_x,
                bounds.min_y,
                bounds.max_x,
                bounds.max_y,
            }));
        if (reduced_index + 1U < reduced_layout.indices.size()) {
            channel.interpolations.push_back(reduced_layout.interpolations[reduced_index]);
        }
    }

    const std::vector<double> sample_times = build_validation_sample_times(
        original_timeline.keyframes,
        0,
        original_timeline.keyframes.size() - 1U);
    for (const double sample_time : sample_times) {
        const std::optional<VectorSample> original_value =
            sample_translate_timeline(original_timeline, sample_time);
        const std::optional<VectorSample> roundtrip_value =
            sample_packed_translate_channel(channel, sample_time, duration);
        if (!original_value.has_value() || !roundtrip_value.has_value()) {
            return false;
        }

        if (std::hypot(
                roundtrip_value->x - original_value->x,
                roundtrip_value->y - original_value->y) > tolerance_pixels) {
            return false;
        }
    }

    *channel_out = std::move(channel);
    return true;
}

AnimationTranslateBounds compute_translate_bounds(const AnimationData& animation) {
    AnimationTranslateBounds bounds;
    for (const BoneTranslateTimeline& timeline : animation.bone_translate_timelines) {
        for (const VectorKeyframe& keyframe : timeline.keyframes) {
            if (!bounds.has_value) {
                bounds.has_value = true;
                bounds.min_x = bounds.max_x = keyframe.x;
                bounds.min_y = bounds.max_y = keyframe.y;
                continue;
            }

            bounds.min_x = std::min(bounds.min_x, static_cast<double>(keyframe.x));
            bounds.min_y = std::min(bounds.min_y, static_cast<double>(keyframe.y));
            bounds.max_x = std::max(bounds.max_x, static_cast<double>(keyframe.x));
            bounds.max_y = std::max(bounds.max_y, static_cast<double>(keyframe.y));
        }
    }
    return bounds;
}

std::vector<CombinedPackedKey> build_combined_keys(const PackedAnimationPayload& animation_payload) {
    std::vector<CombinedPackedKey> combined_keys;
    for (std::size_t channel_index = 0; channel_index < animation_payload.channels.size(); ++channel_index) {
        const PackedAnimationChannel& channel = animation_payload.channels[channel_index];
        for (std::size_t key_index = 0; key_index < channel.times.size(); ++key_index) {
            combined_keys.push_back(CombinedPackedKey{
                channel.times[key_index],
                static_cast<std::uint16_t>(channel_index),
                channel.payloads[key_index],
                channel.bone_index,
                channel.kind,
            });
        }
    }

    std::sort(
        combined_keys.begin(),
        combined_keys.end(),
        [](const CombinedPackedKey& left, const CombinedPackedKey& right) {
            if (left.time != right.time) {
                return left.time < right.time;
            }
            if (left.bone_index != right.bone_index) {
                return left.bone_index < right.bone_index;
            }
            if (left.kind != right.kind) {
                return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
            }
            return left.channel_index < right.channel_index;
        });
    return combined_keys;
}

std::optional<json::LoadError> build_packed_animation_payloads(
    const json::Document& document,
    const BinaryAnimationOptimizationOptions& options,
    std::vector<PackedAnimationPayload>* payloads_out) {
    const SkeletonDataResult skeleton_result = load_skeleton_data(document);
    if (!skeleton_result) {
        return skeleton_result.error;
    }

    payloads_out->clear();
    payloads_out->reserve(skeleton_result.skeleton_data->animations().size());
    for (const AnimationData& animation : skeleton_result.skeleton_data->animations()) {
        PackedAnimationPayload animation_payload;
        animation_payload.name = animation.name;
        animation_payload.duration = animation.duration();

        const AnimationTranslateBounds bounds = compute_translate_bounds(animation);
        if (bounds.has_value) {
            animation_payload.min_translate_x = bounds.min_x;
            animation_payload.min_translate_y = bounds.min_y;
            animation_payload.max_translate_x = bounds.max_x;
            animation_payload.max_translate_y = bounds.max_y;
        }

        for (const BoneRotateTimeline& timeline : animation.bone_rotate_timelines) {
            const ReducedKeyLayout<RotateKeyframe> reduced_layout = reduce_rotate_timeline_layout(
                timeline,
                animation_payload.duration,
                options.rotation_error_tolerance_degrees);
            PackedAnimationChannel channel;
            if (!populate_rotate_channel(
                    timeline,
                    reduced_layout,
                    animation_payload.duration,
                    options.rotation_error_tolerance_degrees,
                    &channel)) {
                channel = {};
                if (!populate_rotate_channel(
                        timeline,
                        ReducedKeyLayout<RotateKeyframe>{
                            [&]() {
                                std::vector<std::size_t> indices(timeline.keyframes.size(), 0U);
                                for (std::size_t index = 0; index < indices.size(); ++index) {
                                    indices[index] = index;
                                }
                                return indices;
                            }(),
                            [&]() {
                                std::vector<PackedInterpolationDescriptor> interpolations;
                                interpolations.reserve(timeline.keyframes.size());
                                for (const RotateKeyframe& keyframe : timeline.keyframes) {
                                    interpolations.push_back(
                                        pack_interpolation_descriptor(keyframe.interpolation));
                                }
                                return interpolations;
                            }(),
                        },
                        animation_payload.duration,
                        options.rotation_error_tolerance_degrees,
                        &channel)) {
                    return make_write_failure(
                        document.source_path,
                        "failed to quantize rotate timeline for animation '" + animation.name + "'");
                }
            }
            animation_payload.channels.push_back(std::move(channel));
        }

        for (const BoneTranslateTimeline& timeline : animation.bone_translate_timelines) {
            const ReducedKeyLayout<VectorKeyframe> reduced_layout =
                reduce_translate_timeline_layout(
                    timeline,
                    animation_payload.duration,
                    bounds,
                    options.translation_error_tolerance);
            PackedAnimationChannel channel;
            if (!populate_translate_channel(
                    timeline,
                    reduced_layout,
                    animation_payload.duration,
                    bounds,
                    options.translation_error_tolerance,
                    &channel)) {
                channel = {};
                if (!populate_translate_channel(
                        timeline,
                        ReducedKeyLayout<VectorKeyframe>{
                            [&]() {
                                std::vector<std::size_t> indices(timeline.keyframes.size(), 0U);
                                for (std::size_t index = 0; index < indices.size(); ++index) {
                                    indices[index] = index;
                                }
                                return indices;
                            }(),
                            [&]() {
                                std::vector<PackedInterpolationDescriptor> interpolations;
                                interpolations.reserve(timeline.keyframes.size());
                                for (const VectorKeyframe& keyframe : timeline.keyframes) {
                                    interpolations.push_back(
                                        pack_interpolation_descriptor(keyframe.interpolation));
                                }
                                return interpolations;
                            }(),
                        },
                        animation_payload.duration,
                        bounds,
                        options.translation_error_tolerance,
                        &channel)) {
                    return make_write_failure(
                        document.source_path,
                        "failed to quantize translate timeline for animation '" + animation.name + "'");
                }
            }
            animation_payload.channels.push_back(std::move(channel));
        }

        const std::vector<CombinedPackedKey> combined_keys = build_combined_keys(animation_payload);
        animation_payload.keyframes_sorted_by_time_and_bone = std::is_sorted(
            combined_keys.begin(),
            combined_keys.end(),
            [](const CombinedPackedKey& left, const CombinedPackedKey& right) {
                if (left.time != right.time) {
                    return left.time < right.time;
                }
                if (left.bone_index != right.bone_index) {
                    return left.bone_index < right.bone_index;
                }
                if (left.kind != right.kind) {
                    return static_cast<std::uint8_t>(left.kind) <
                        static_cast<std::uint8_t>(right.kind);
                }
                return left.channel_index <= right.channel_index;
            });
        payloads_out->push_back(std::move(animation_payload));
    }

    return std::nullopt;
}

void encode_animation_section(
    const std::vector<PackedAnimationPayload>& animation_payloads,
    const StringTableBuilder& strings,
    std::vector<std::uint8_t>& bytes) {
    bytes.insert(
        bytes.end(),
        std::begin(kAnimationSectionMagic),
        std::end(kAnimationSectionMagic));
    append_varint(bytes, static_cast<std::uint64_t>(animation_payloads.size()));
    for (const PackedAnimationPayload& animation_payload : animation_payloads) {
        append_varint(bytes, strings.indices.at(animation_payload.name));
        append_float32(bytes, animation_payload.duration);
        append_float32(bytes, animation_payload.min_translate_x);
        append_float32(bytes, animation_payload.min_translate_y);
        append_float32(bytes, animation_payload.max_translate_x);
        append_float32(bytes, animation_payload.max_translate_y);

        append_varint(bytes, static_cast<std::uint64_t>(animation_payload.channels.size()));
        for (const PackedAnimationChannel& channel : animation_payload.channels) {
            append_varint(bytes, static_cast<std::uint64_t>(channel.bone_index));
            bytes.push_back(static_cast<std::uint8_t>(channel.kind));
            append_varint(bytes, static_cast<std::uint64_t>(channel.times.size()));
        }

        const std::vector<CombinedPackedKey> combined_keys = build_combined_keys(animation_payload);
        append_varint(bytes, static_cast<std::uint64_t>(combined_keys.size()));
        for (const CombinedPackedKey& key : combined_keys) {
            append_uint16(bytes, key.time);
            append_uint16(bytes, key.channel_index);
            append_uint32(bytes, key.payload);
        }

        for (const PackedAnimationChannel& channel : animation_payload.channels) {
            for (const PackedInterpolationDescriptor& interpolation : channel.interpolations) {
                bytes.push_back(static_cast<std::uint8_t>(interpolation.kind));
                if (interpolation.kind != InterpolationKind::CubicBezier) {
                    continue;
                }

                append_float32(bytes, interpolation.cubic.cx1);
                append_float32(bytes, interpolation.cubic.cy1);
                append_float32(bytes, interpolation.cubic.cx2);
                append_float32(bytes, interpolation.cubic.cy2);
            }
        }
    }
}

class BinaryDecoder {
public:
    BinaryDecoder(std::string_view bytes, std::filesystem::path source_path)
        : bytes_(bytes),
          source_path_(std::move(source_path)) {}

    json::LoadResult decode_document(std::uint64_t* version_out) {
        if (bytes_.size() < sizeof(kBinaryMagic) ||
            !std::equal(std::begin(kBinaryMagic), std::end(kBinaryMagic), bytes_.begin())) {
            return make_failure(source_path_, "invalid Marrow binary asset header");
        }
        offset_ = sizeof(kBinaryMagic);

        const std::optional<std::uint64_t> version = read_varint("binary asset version");
        if (!version.has_value()) {
            return finish_failure();
        }
        if (*version != kBinaryVersionGenericDocument &&
            *version != kBinaryVersionPackedAnimations) {
            return make_failure(source_path_, "unsupported Marrow binary asset version");
        }
        *version_out = *version;

        const std::optional<std::uint64_t> string_count = read_varint("string table count");
        if (!string_count.has_value()) {
            return finish_failure();
        }
        if (*string_count > bytes_.size()) {
            return make_failure(source_path_, "binary asset string table count is implausibly large");
        }
        string_table_.reserve(static_cast<std::size_t>(*string_count));
        for (std::uint64_t index = 0; index < *string_count; ++index) {
            const std::optional<std::uint64_t> string_size =
                read_varint("string table entry length");
            if (!string_size.has_value()) {
                return finish_failure();
            }
            if (!require_bytes(
                    static_cast<std::size_t>(*string_size),
                    "string table entry bytes")) {
                return finish_failure();
            }

            string_table_.emplace_back(
                bytes_.substr(offset_, static_cast<std::size_t>(*string_size)));
            offset_ += static_cast<std::size_t>(*string_size);
        }

        const std::optional<std::uint64_t> boolean_count = read_varint("boolean bitfield count");
        if (!boolean_count.has_value()) {
            return finish_failure();
        }
        if (*boolean_count > static_cast<std::uint64_t>(bytes_.size()) * 8U) {
            return make_failure(source_path_, "binary asset boolean bitfield count is implausibly large");
        }
        const std::size_t packed_boolean_bytes =
            static_cast<std::size_t>((*boolean_count + 7U) / 8U);
        if (!require_bytes(packed_boolean_bytes, "boolean bitfield bytes")) {
            return finish_failure();
        }
        booleans_.reserve(static_cast<std::size_t>(*boolean_count));
        for (std::uint64_t index = 0; index < *boolean_count; ++index) {
            const std::uint8_t packed = static_cast<std::uint8_t>(
                bytes_[offset_ + static_cast<std::size_t>(index / 8U)]);
            booleans_.push_back((packed >> (index % 8U)) & 0x01U);
        }
        offset_ += packed_boolean_bytes;

        json::Value root = decode_value(0);
        if (has_error()) {
            return finish_failure();
        }

        if (boolean_index_ != booleans_.size()) {
            return make_failure(source_path_, "binary asset left unused boolean payload data");
        }

        json::Document document;
        document.source_path = source_path_;
        document.root = std::move(root);

        json::LoadResult result;
        result.document = std::move(document);
        return result;
    }

    std::optional<json::LoadError> decode_animation_section(
        json::Document* document,
        bool apply_animation_optimizations,
        SkeletonBinaryInspection* inspection_out) {
        if (!require_bytes(sizeof(kAnimationSectionMagic), "packed animation section header")) {
            return error_;
        }
        if (!std::equal(
                std::begin(kAnimationSectionMagic),
                std::end(kAnimationSectionMagic),
                bytes_.begin() + static_cast<std::ptrdiff_t>(offset_))) {
            return make_write_failure(
                source_path_,
                "binary asset is missing the packed animation section header");
        }
        offset_ += sizeof(kAnimationSectionMagic);

        const std::optional<std::uint64_t> animation_count =
            read_varint("packed animation count");
        if (!animation_count.has_value()) {
            return error_;
        }

        if (inspection_out != nullptr) {
            inspection_out->has_optimized_animation_section = true;
            inspection_out->animation_count = static_cast<std::size_t>(*animation_count);
            inspection_out->keyframes_sorted_by_time_and_bone = true;
        }

        std::vector<std::string> bone_names;
        if (apply_animation_optimizations) {
            if (!extract_bone_names(document->root, &bone_names)) {
                return fail_error(
                    "packed animation section could not map bone indices back to names");
            }
        }

        for (std::uint64_t animation_index = 0; animation_index < *animation_count; ++animation_index) {
            PackedAnimationPayload payload;
            const std::optional<std::uint64_t> animation_name_index =
                read_varint("packed animation name");
            if (!animation_name_index.has_value()) {
                return error_;
            }
            if (*animation_name_index >= string_table_.size()) {
                return fail_error("packed animation name index was out of range");
            }
            payload.name = string_table_[static_cast<std::size_t>(*animation_name_index)];

            const std::optional<float> duration = read_float32("packed animation duration");
            const std::optional<float> min_x = read_float32("packed animation min x");
            const std::optional<float> min_y = read_float32("packed animation min y");
            const std::optional<float> max_x = read_float32("packed animation max x");
            const std::optional<float> max_y = read_float32("packed animation max y");
            if (!duration.has_value() || !min_x.has_value() || !min_y.has_value() ||
                !max_x.has_value() || !max_y.has_value()) {
                return error_;
            }
            payload.duration = *duration;
            payload.min_translate_x = *min_x;
            payload.min_translate_y = *min_y;
            payload.max_translate_x = *max_x;
            payload.max_translate_y = *max_y;

            const std::optional<std::uint64_t> channel_count =
                read_varint("packed animation channel count");
            if (!channel_count.has_value()) {
                return error_;
            }
            payload.channels.reserve(static_cast<std::size_t>(*channel_count));
            for (std::uint64_t channel_index = 0; channel_index < *channel_count; ++channel_index) {
                const std::optional<std::uint64_t> bone_index =
                    read_varint("packed animation bone index");
                const std::optional<std::uint8_t> raw_kind =
                    read_byte("packed animation channel kind");
                const std::optional<std::uint64_t> key_count =
                    read_varint("packed animation key count");
                if (!bone_index.has_value() || !raw_kind.has_value() || !key_count.has_value()) {
                    return error_;
                }
                if (*bone_index > std::numeric_limits<std::size_t>::max()) {
                    return fail_error("packed animation bone index was implausibly large");
                }
                if (*key_count == 0) {
                    return fail_error("packed animation channels must contain at least one keyframe");
                }

                PackedAnimationChannel channel;
                channel.bone_index = static_cast<std::size_t>(*bone_index);
                if (*raw_kind == static_cast<std::uint8_t>(PackedChannelKind::Rotate)) {
                    channel.kind = PackedChannelKind::Rotate;
                    if (inspection_out != nullptr) {
                        ++inspection_out->rotate_channel_count;
                    }
                } else if (*raw_kind == static_cast<std::uint8_t>(PackedChannelKind::Translate)) {
                    channel.kind = PackedChannelKind::Translate;
                    if (inspection_out != nullptr) {
                        ++inspection_out->translate_channel_count;
                    }
                } else {
                    return fail_error("packed animation channel kind was unknown");
                }
                channel.times.reserve(static_cast<std::size_t>(*key_count));
                channel.payloads.reserve(static_cast<std::size_t>(*key_count));
                payload.channels.push_back(std::move(channel));
            }

            const std::optional<std::uint64_t> total_key_count =
                read_varint("packed animation total key count");
            if (!total_key_count.has_value()) {
                return error_;
            }
            std::vector<CombinedPackedKey> combined_keys;
            combined_keys.reserve(static_cast<std::size_t>(*total_key_count));
            for (std::uint64_t key_index = 0; key_index < *total_key_count; ++key_index) {
                const std::optional<std::uint16_t> time =
                    read_uint16("packed keyframe time");
                const std::optional<std::uint16_t> channel_index =
                    read_uint16("packed keyframe channel index");
                const std::optional<std::uint32_t> payload_value =
                    read_uint32("packed keyframe payload");
                if (!time.has_value() || !channel_index.has_value() || !payload_value.has_value()) {
                    return error_;
                }
                if (*channel_index >= payload.channels.size()) {
                    return fail_error("packed keyframe channel index was out of range");
                }

                const PackedAnimationChannel& channel = payload.channels[*channel_index];
                combined_keys.push_back(CombinedPackedKey{
                    *time,
                    *channel_index,
                    *payload_value,
                    channel.bone_index,
                    channel.kind,
                });
            }
            if (inspection_out != nullptr) {
                inspection_out->optimized_keyframe_count += combined_keys.size();
                if (!std::is_sorted(
                        combined_keys.begin(),
                        combined_keys.end(),
                        [](const CombinedPackedKey& left, const CombinedPackedKey& right) {
                            if (left.time != right.time) {
                                return left.time < right.time;
                            }
                            if (left.bone_index != right.bone_index) {
                                return left.bone_index < right.bone_index;
                            }
                            if (left.kind != right.kind) {
                                return static_cast<std::uint8_t>(left.kind) <
                                    static_cast<std::uint8_t>(right.kind);
                            }
                            return left.channel_index <= right.channel_index;
                        })) {
                    inspection_out->keyframes_sorted_by_time_and_bone = false;
                }
            }

            for (const CombinedPackedKey& key : combined_keys) {
                PackedAnimationChannel& channel = payload.channels[key.channel_index];
                if (!channel.times.empty() && key.time <= channel.times.back()) {
                    return fail_error(
                        "packed keyframes were not strictly increasing within a channel");
                }
                channel.times.push_back(key.time);
                channel.payloads.push_back(key.payload);
            }

            for (PackedAnimationChannel& channel : payload.channels) {
                if (channel.times.size() < 2U) {
                    continue;
                }

                channel.interpolations.reserve(channel.times.size() - 1U);
                for (std::size_t interpolation_index = 0;
                     interpolation_index + 1U < channel.times.size();
                     ++interpolation_index) {
                    const std::optional<std::uint8_t> raw_kind =
                        read_byte("packed interpolation kind");
                    if (!raw_kind.has_value()) {
                        return error_;
                    }

                    PackedInterpolationDescriptor descriptor;
                    if (*raw_kind == static_cast<std::uint8_t>(InterpolationKind::Linear)) {
                        descriptor.kind = InterpolationKind::Linear;
                    } else if (*raw_kind == static_cast<std::uint8_t>(InterpolationKind::Stepped)) {
                        descriptor.kind = InterpolationKind::Stepped;
                    } else if (*raw_kind ==
                               static_cast<std::uint8_t>(InterpolationKind::CubicBezier)) {
                        descriptor.kind = InterpolationKind::CubicBezier;
                        const std::optional<float> cx1 = read_float32("packed cubic cx1");
                        const std::optional<float> cy1 = read_float32("packed cubic cy1");
                        const std::optional<float> cx2 = read_float32("packed cubic cx2");
                        const std::optional<float> cy2 = read_float32("packed cubic cy2");
                        if (!cx1.has_value() || !cy1.has_value() || !cx2.has_value() ||
                            !cy2.has_value()) {
                            return error_;
                        }
                        descriptor.cubic = {*cx1, *cy1, *cx2, *cy2};
                    } else {
                        return fail_error("packed interpolation kind was unknown");
                    }
                    channel.interpolations.push_back(descriptor);
                }
            }

            decode_channel_values(&payload);
            if (apply_animation_optimizations &&
                !apply_animation_payload(document, bone_names, payload)) {
                return fail_error(
                    "packed animation section could not rebuild animation '" + payload.name + "'");
            }
        }

        return std::nullopt;
    }

    bool consumed_all() const {
        return offset_ == bytes_.size();
    }

private:
    json::Value decode_value(std::size_t depth) {
        if (depth > kMaxDecodeDepth) {
            return fail("binary asset nesting exceeds the supported depth");
        }

        const json::SourceLocation location = current_location();
        const std::optional<std::uint8_t> raw_tag = read_byte("node tag");
        if (!raw_tag.has_value()) {
            return json::Value(location);
        }

        switch (static_cast<NodeTag>(*raw_tag)) {
        case NodeTag::Null:
            return json::Value(nullptr, location);
        case NodeTag::Boolean:
            if (boolean_index_ >= booleans_.size()) {
                return fail("binary asset boolean node exceeded the bitfield payload");
            }
            return json::Value(booleans_[boolean_index_++] != 0U, location);
        case NodeTag::Number: {
            const std::optional<float> number = read_float32("number payload");
            if (!number.has_value()) {
                return json::Value(location);
            }
            return json::Value(static_cast<double>(*number), location);
        }
        case NodeTag::String: {
            const std::optional<std::uint64_t> string_index =
                read_varint("string table index");
            if (!string_index.has_value()) {
                return json::Value(location);
            }
            if (*string_index >= string_table_.size()) {
                return fail("binary asset string table index was out of range");
            }
            return json::Value(
                string_table_[static_cast<std::size_t>(*string_index)],
                location);
        }
        case NodeTag::Array: {
            const std::optional<std::uint64_t> array_size = read_varint("array length");
            if (!array_size.has_value()) {
                return json::Value(location);
            }
            if (*array_size > bytes_.size() - offset_) {
                return fail("binary asset array length is implausibly large");
            }

            json::Value::Array array;
            array.reserve(static_cast<std::size_t>(*array_size));
            for (std::uint64_t index = 0; index < *array_size; ++index) {
                array.push_back(decode_value(depth + 1U));
                if (has_error()) {
                    return json::Value(location);
                }
            }
            return json::Value(std::move(array), location);
        }
        case NodeTag::Object: {
            const std::optional<std::uint64_t> member_count = read_varint("object member count");
            if (!member_count.has_value()) {
                return json::Value(location);
            }
            if (*member_count > bytes_.size() - offset_) {
                return fail("binary asset object member count is implausibly large");
            }

            json::Value::Object object;
            for (std::uint64_t index = 0; index < *member_count; ++index) {
                const std::optional<std::uint64_t> string_index =
                    read_varint("object member key index");
                if (!string_index.has_value()) {
                    return json::Value(location);
                }
                if (*string_index >= string_table_.size()) {
                    return fail("binary asset object key index was out of range");
                }

                const std::string& key =
                    string_table_[static_cast<std::size_t>(*string_index)];
                json::Value value = decode_value(depth + 1U);
                if (has_error()) {
                    return json::Value(location);
                }

                const auto [iterator, inserted] = object.emplace(key, std::move(value));
                if (!inserted) {
                    return fail("binary asset object contained a duplicate key");
                }
            }
            return json::Value(std::move(object), location);
        }
        }

        return fail("binary asset used an unknown node tag");
    }

    bool extract_bone_names(const json::Value& root, std::vector<std::string>* bone_names_out) {
        if (!root.is_object()) {
            return false;
        }

        const json::Value* bones_value = json::find_member(root, "bones");
        if (bones_value == nullptr || !bones_value->is_array()) {
            return false;
        }

        bone_names_out->clear();
        bone_names_out->reserve(bones_value->as_array().size());
        for (const json::Value& bone_value : bones_value->as_array()) {
            if (!bone_value.is_object()) {
                return false;
            }
            const json::Value* bone_name = json::find_member(bone_value, "name");
            if (bone_name == nullptr || !bone_name->is_string()) {
                return false;
            }
            bone_names_out->push_back(bone_name->as_string());
        }
        return true;
    }

    void decode_channel_values(PackedAnimationPayload* payload) {
        for (PackedAnimationChannel& channel : payload->channels) {
            if (channel.kind == PackedChannelKind::Rotate) {
                channel.rotate_values.reserve(channel.payloads.size());
                for (const std::uint32_t payload_value : channel.payloads) {
                    channel.rotate_values.push_back(
                        decode_rotation_degrees(
                            static_cast<std::uint16_t>(payload_value & 0xFFFFU)));
                }
                continue;
            }

            channel.translate_values.reserve(channel.payloads.size());
            for (const std::uint32_t payload_value : channel.payloads) {
                channel.translate_values.push_back(
                    decode_translate_payload(payload_value, *payload));
            }
        }
    }

    bool apply_animation_payload(
        json::Document* document,
        const std::vector<std::string>& bone_names,
        const PackedAnimationPayload& payload) {
        if (document == nullptr || !document->root.is_object()) {
            return false;
        }

        json::Value* animations_value =
            json::find_member(document->root, "animations");
        if (animations_value == nullptr || !animations_value->is_object()) {
            return false;
        }

        auto animation_iterator = animations_value->as_object().find(payload.name);
        if (animation_iterator == animations_value->as_object().end() ||
            !animation_iterator->second.is_object()) {
            return false;
        }

        json::Value& animation_value = animation_iterator->second;
        json::Value* bones_value = json::find_member(animation_value, "bones");
        if (bones_value == nullptr) {
            animation_value.as_object().emplace(
                "bones",
                json::Value(json::Value::Object{}, animation_value.location()));
            bones_value = json::find_member(animation_value, "bones");
        }
        if (bones_value == nullptr || !bones_value->is_object()) {
            return false;
        }

        for (const PackedAnimationChannel& channel : payload.channels) {
            if (channel.bone_index >= bone_names.size()) {
                return false;
            }

            const std::string& bone_name = bone_names[channel.bone_index];
            auto bone_iterator = bones_value->as_object().find(bone_name);
            if (bone_iterator == bones_value->as_object().end()) {
                bone_iterator = bones_value->as_object()
                                    .emplace(
                                        bone_name,
                                        json::Value(
                                            json::Value::Object{},
                                            bones_value->location()))
                                    .first;
            }
            if (!bone_iterator->second.is_object()) {
                return false;
            }

            json::Value::Array keyframes;
            keyframes.reserve(channel.times.size());
            for (std::size_t key_index = 0; key_index < channel.times.size(); ++key_index) {
                json::Value::Object keyframe_object;
                keyframe_object.emplace(
                    "time",
                    json::Value(
                        decode_time_value(channel.times[key_index], payload.duration),
                        animation_value.location()));
                if (channel.kind == PackedChannelKind::Rotate) {
                    keyframe_object.emplace(
                        "angle",
                        json::Value(channel.rotate_values[key_index], animation_value.location()));
                } else {
                    keyframe_object.emplace(
                        "x",
                        json::Value(
                            channel.translate_values[key_index].x,
                            animation_value.location()));
                    keyframe_object.emplace(
                        "y",
                        json::Value(
                            channel.translate_values[key_index].y,
                            animation_value.location()));
                }

                if (key_index + 1U < channel.times.size()) {
                    const PackedInterpolationDescriptor& interpolation =
                        channel.interpolations[key_index];
                    if (interpolation.kind == InterpolationKind::Stepped) {
                        keyframe_object.emplace(
                            "curve",
                            json::Value(std::string("stepped"), animation_value.location()));
                    } else if (interpolation.kind == InterpolationKind::CubicBezier) {
                        json::Value::Array curve;
                        curve.emplace_back(interpolation.cubic.cx1, animation_value.location());
                        curve.emplace_back(interpolation.cubic.cy1, animation_value.location());
                        curve.emplace_back(interpolation.cubic.cx2, animation_value.location());
                        curve.emplace_back(interpolation.cubic.cy2, animation_value.location());
                        keyframe_object.emplace(
                            "curve",
                            json::Value(std::move(curve), animation_value.location()));
                    }
                }

                keyframes.emplace_back(std::move(keyframe_object), animation_value.location());
            }

            bone_iterator->second.as_object()[channel.kind == PackedChannelKind::Rotate
                                                  ? "rotate"
                                                  : "translate"] =
                json::Value(std::move(keyframes), animation_value.location());
        }

        return true;
    }

    json::Value fail(std::string message) {
        if (!error_.has_value()) {
            error_ = json::LoadError{
                source_path_,
                current_location(),
                std::move(message),
                {},
                {},
            };
        }
        return json::Value(current_location());
    }

    std::optional<json::LoadError> fail_error(std::string message) {
        if (!error_.has_value()) {
            error_ = json::LoadError{
                source_path_,
                current_location(),
                std::move(message),
                {},
                {},
            };
        }
        return error_;
    }

    bool require_bytes(std::size_t count, std::string_view context) {
        if (count <= bytes_.size() - offset_) {
            return true;
        }

        if (!error_.has_value()) {
            error_ = json::LoadError{
                source_path_,
                current_location(),
                "binary asset ended unexpectedly while reading " + std::string(context),
                {},
                {},
            };
        }
        return false;
    }

    std::optional<std::uint8_t> read_byte(std::string_view context) {
        if (!require_bytes(1, context)) {
            return std::nullopt;
        }

        return static_cast<std::uint8_t>(bytes_[offset_++]);
    }

    std::optional<std::uint16_t> read_uint16(std::string_view context) {
        if (!require_bytes(sizeof(std::uint16_t), context)) {
            return std::nullopt;
        }

        const std::uint16_t value =
            static_cast<std::uint16_t>(
                static_cast<std::uint8_t>(bytes_[offset_ + 0U])) |
            static_cast<std::uint16_t>(
                static_cast<std::uint8_t>(bytes_[offset_ + 1U]))
                << 8U;
        offset_ += sizeof(std::uint16_t);
        return value;
    }

    std::optional<std::uint32_t> read_uint32(std::string_view context) {
        if (!require_bytes(sizeof(std::uint32_t), context)) {
            return std::nullopt;
        }

        std::uint32_t value = 0;
        value |= static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(bytes_[offset_ + 0U]));
        value |= static_cast<std::uint32_t>(
                     static_cast<std::uint8_t>(bytes_[offset_ + 1U]))
            << 8U;
        value |= static_cast<std::uint32_t>(
                     static_cast<std::uint8_t>(bytes_[offset_ + 2U]))
            << 16U;
        value |= static_cast<std::uint32_t>(
                     static_cast<std::uint8_t>(bytes_[offset_ + 3U]))
            << 24U;
        offset_ += sizeof(std::uint32_t);
        return value;
    }

    std::optional<std::uint64_t> read_varint(std::string_view context) {
        std::uint64_t value = 0;
        int shift = 0;
        for (int byte_index = 0; byte_index < 10; ++byte_index) {
            const std::optional<std::uint8_t> byte = read_byte(context);
            if (!byte.has_value()) {
                return std::nullopt;
            }

            value |= static_cast<std::uint64_t>(*byte & 0x7FU) << shift;
            if ((*byte & 0x80U) == 0U) {
                return value;
            }
            shift += 7;
        }

        if (!error_.has_value()) {
            error_ = json::LoadError{
                source_path_,
                current_location(),
                "binary asset varint exceeded the supported width while reading " +
                    std::string(context),
                {},
                {},
            };
        }
        return std::nullopt;
    }

    std::optional<float> read_float32(std::string_view context) {
        const std::optional<std::uint32_t> raw = read_uint32(context);
        if (!raw.has_value()) {
            return std::nullopt;
        }

        float value = 0.0F;
        std::memcpy(&value, &(*raw), sizeof(value));
        return value;
    }

    json::LoadResult finish_failure() const {
        json::LoadResult result;
        result.error = error_;
        return result;
    }

    bool has_error() const {
        return error_.has_value();
    }

    json::SourceLocation current_location() const {
        return json::SourceLocation{1U, offset_ + 1U, offset_};
    }

    std::string_view bytes_;
    std::filesystem::path source_path_;
    std::size_t offset_{0};
    std::vector<std::string> string_table_;
    std::vector<std::uint8_t> booleans_;
    std::size_t boolean_index_{0};
    std::optional<json::LoadError> error_;
};

json::LoadResult load_binary_document_impl(
    const std::filesystem::path& path,
    bool apply_animation_optimizations,
    SkeletonBinaryInspection* inspection_out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return make_failure(path, "failed to open file");
    }

    std::string bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        return make_failure(path, "failed while reading file");
    }

    BinaryDecoder decoder(bytes, path);
    std::uint64_t version = 0;
    json::LoadResult result = decoder.decode_document(&version);
    if (!result) {
        return result;
    }

    if (inspection_out != nullptr) {
        *inspection_out = {};
        inspection_out->binary_version = version;
    }

    if (version == kBinaryVersionPackedAnimations) {
        if (const std::optional<json::LoadError> error = decoder.decode_animation_section(
                &(*result.document),
                apply_animation_optimizations,
                inspection_out)) {
            result.document.reset();
            result.error = *error;
            return result;
        }
    }

    if (!decoder.consumed_all()) {
        return make_failure(path, "binary asset has trailing payload bytes");
    }

    return result;
}

} // namespace

std::string_view skeleton_binary_extension() {
    return ".mbin";
}

json::LoadResult load_skeleton_document(const std::filesystem::path& path) {
    if (path.extension() != skeleton_binary_extension()) {
        return json::load_document(path);
    }

    return detail::load_binary_skeleton_document(path, false, nullptr);
}

std::optional<json::LoadError> write_skeleton_binary_document(
    const json::Document& document,
    const std::filesystem::path& path,
    const BinaryAnimationOptimizationOptions& options) {
    StringTableBuilder strings;
    collect_strings(document.root, strings);

    std::vector<PackedAnimationPayload> animation_payloads;
    if (const auto error = build_packed_animation_payloads(
            document,
            options,
            &animation_payloads)) {
        return error;
    }
    for (const PackedAnimationPayload& animation_payload : animation_payloads) {
        strings.intern(animation_payload.name);
    }

    std::vector<std::uint8_t> booleans;
    collect_booleans(document.root, booleans);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(2048);
    bytes.insert(bytes.end(), std::begin(kBinaryMagic), std::end(kBinaryMagic));
    append_varint(bytes, kBinaryVersionPackedAnimations);
    append_varint(bytes, static_cast<std::uint64_t>(strings.values.size()));
    for (const std::string& value : strings.values) {
        append_varint(bytes, static_cast<std::uint64_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    append_varint(bytes, static_cast<std::uint64_t>(booleans.size()));
    const std::vector<std::uint8_t> packed_booleans = pack_booleans(booleans);
    bytes.insert(bytes.end(), packed_booleans.begin(), packed_booleans.end());
    encode_value(document.root, strings, bytes);
    encode_animation_section(animation_payloads, strings, bytes);

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return make_write_failure(path, "failed to open file");
    }

    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output.good()) {
        return make_write_failure(path, "failed while writing file");
    }

    return std::nullopt;
}

std::optional<json::LoadError> inspect_skeleton_binary(
    const std::filesystem::path& path,
    SkeletonBinaryInspection* inspection_out) {
    if (path.extension() != skeleton_binary_extension()) {
        return make_write_failure(path, "binary inspection requires a .mbin path");
    }

    const json::LoadResult result = detail::load_binary_skeleton_document(path, false, inspection_out);
    if (result) {
        return std::nullopt;
    }

    return result.error;
}

namespace detail {

json::LoadResult load_binary_skeleton_document(
    const std::filesystem::path& path,
    bool apply_animation_optimizations,
    SkeletonBinaryInspection* inspection_out) {
    return load_binary_document_impl(path, apply_animation_optimizations, inspection_out);
}

} // namespace detail

} // namespace marrow::runtime
