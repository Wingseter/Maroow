#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::runtime {

struct AnimationRoundtripMetrics {
    double max_rotation_error_degrees{0.0};
    double max_translation_error_pixels{0.0};
    std::size_t original_rotation_keyframes{0};
    std::size_t roundtrip_rotation_keyframes{0};
    std::size_t original_translation_keyframes{0};
    std::size_t roundtrip_translation_keyframes{0};
};

struct AnimationRoundtripComparison {
    AnimationRoundtripMetrics metrics;
    std::optional<std::string> error;

    /// @brief Reports whether the comparison completed without detecting an error.
    /// @return `true` when no comparison error was produced; otherwise `false`.
    explicit operator bool() const {
        return !error.has_value();
    }
};

namespace detail {

inline double normalize_rotation_error_degrees(double angle) {
    return angle - (std::ceil(angle / 360.0 - 0.5) * 360.0);
}

template <typename Timeline>
inline std::size_t count_timeline_keyframes(const std::vector<Timeline>& timelines) {
    std::size_t count = 0;
    for (const Timeline& timeline : timelines) {
        count += timeline.keyframes.size();
    }
    return count;
}

inline void append_sample_time(std::vector<double>* sample_times, double time) {
    constexpr double kSampleTimeEpsilon = 1e-6;
    for (const double existing : *sample_times) {
        if (std::abs(existing - time) <= kSampleTimeEpsilon) {
            return;
        }
    }
    sample_times->push_back(time);
}

inline void append_rotation_bones(
    std::vector<std::size_t>* bone_indices,
    const std::vector<BoneRotateTimeline>& timelines) {
    for (const BoneRotateTimeline& timeline : timelines) {
        if (std::find(bone_indices->begin(), bone_indices->end(), timeline.bone_index) ==
            bone_indices->end()) {
            bone_indices->push_back(timeline.bone_index);
        }
    }
}

inline void append_translation_bones(
    std::vector<std::size_t>* bone_indices,
    const std::vector<BoneTranslateTimeline>& timelines) {
    for (const BoneTranslateTimeline& timeline : timelines) {
        if (std::find(bone_indices->begin(), bone_indices->end(), timeline.bone_index) ==
            bone_indices->end()) {
            bone_indices->push_back(timeline.bone_index);
        }
    }
}

template <typename Timeline>
inline void append_timeline_key_times(
    std::vector<double>* sample_times,
    const std::vector<Timeline>& timelines) {
    for (const Timeline& timeline : timelines) {
        for (const auto& keyframe : timeline.keyframes) {
            append_sample_time(sample_times, keyframe.time);
        }
    }
}

inline std::vector<double> build_animation_sample_times(
    const AnimationData& original,
    const AnimationData& roundtrip,
    std::size_t uniform_sample_count) {
    std::vector<double> sample_times;
    append_sample_time(&sample_times, 0.0);

    const double duration = std::max(original.duration(), roundtrip.duration());
    if (duration > 0.0 && uniform_sample_count > 0) {
        for (std::size_t index = 0; index <= uniform_sample_count; ++index) {
            append_sample_time(
                &sample_times,
                duration * static_cast<double>(index) /
                    static_cast<double>(uniform_sample_count));
        }
    }

    append_timeline_key_times(&sample_times, original.bone_rotate_timelines);
    append_timeline_key_times(&sample_times, roundtrip.bone_rotate_timelines);
    append_timeline_key_times(&sample_times, original.bone_translate_timelines);
    append_timeline_key_times(&sample_times, roundtrip.bone_translate_timelines);

    std::sort(sample_times.begin(), sample_times.end());
    return sample_times;
}

} // namespace detail

/**
 * @brief Compares original and round-tripped animation payloads across sampled times.
 * @param original Source skeleton data before round-trip conversion.
 * @param roundtrip Skeleton data after round-trip conversion.
 * @param uniform_sample_count Additional evenly spaced samples per animation duration.
 * @return Error metrics and an optional mismatch description.
 */
inline AnimationRoundtripComparison compare_animation_roundtrip(
    const SkeletonData& original,
    const SkeletonData& roundtrip,
    std::size_t uniform_sample_count = 256) {
    AnimationRoundtripComparison comparison;

    for (const AnimationData& animation : original.animations()) {
        comparison.metrics.original_rotation_keyframes +=
            detail::count_timeline_keyframes(animation.bone_rotate_timelines);
        comparison.metrics.original_translation_keyframes +=
            detail::count_timeline_keyframes(animation.bone_translate_timelines);

        const AnimationData* roundtrip_animation = roundtrip.find_animation(animation.name);
        if (roundtrip_animation == nullptr) {
            comparison.error =
                "roundtrip asset is missing animation '" + animation.name + "'";
            return comparison;
        }

        comparison.metrics.roundtrip_rotation_keyframes +=
            detail::count_timeline_keyframes(roundtrip_animation->bone_rotate_timelines);
        comparison.metrics.roundtrip_translation_keyframes +=
            detail::count_timeline_keyframes(roundtrip_animation->bone_translate_timelines);

        std::vector<std::size_t> rotation_bones;
        detail::append_rotation_bones(&rotation_bones, animation.bone_rotate_timelines);
        detail::append_rotation_bones(&rotation_bones, roundtrip_animation->bone_rotate_timelines);
        std::sort(rotation_bones.begin(), rotation_bones.end());

        std::vector<std::size_t> translation_bones;
        detail::append_translation_bones(&translation_bones, animation.bone_translate_timelines);
        detail::append_translation_bones(
            &translation_bones,
            roundtrip_animation->bone_translate_timelines);
        std::sort(translation_bones.begin(), translation_bones.end());

        const std::vector<double> sample_times = detail::build_animation_sample_times(
            animation,
            *roundtrip_animation,
            uniform_sample_count);
        for (const double sample_time : sample_times) {
            for (const std::size_t bone_index : rotation_bones) {
                const std::optional<double> original_rotation =
                    animation.sample_bone_rotation(bone_index, sample_time);
                const std::optional<double> roundtrip_rotation =
                    roundtrip_animation->sample_bone_rotation(bone_index, sample_time);
                if (original_rotation.has_value() != roundtrip_rotation.has_value()) {
                    comparison.error =
                        "rotation channel mismatch for animation '" + animation.name + "'";
                    return comparison;
                }
                if (!original_rotation.has_value()) {
                    continue;
                }

                comparison.metrics.max_rotation_error_degrees = std::max(
                    comparison.metrics.max_rotation_error_degrees,
                    std::abs(detail::normalize_rotation_error_degrees(
                        *roundtrip_rotation - *original_rotation)));
            }

            for (const std::size_t bone_index : translation_bones) {
                const std::optional<VectorSample> original_translation =
                    animation.sample_bone_translation(bone_index, sample_time);
                const std::optional<VectorSample> roundtrip_translation =
                    roundtrip_animation->sample_bone_translation(bone_index, sample_time);
                if (original_translation.has_value() != roundtrip_translation.has_value()) {
                    comparison.error =
                        "translation channel mismatch for animation '" + animation.name + "'";
                    return comparison;
                }
                if (!original_translation.has_value()) {
                    continue;
                }

                comparison.metrics.max_translation_error_pixels = std::max(
                    comparison.metrics.max_translation_error_pixels,
                    std::hypot(
                        roundtrip_translation->x - original_translation->x,
                        roundtrip_translation->y - original_translation->y));
            }
        }
    }

    for (const AnimationData& animation : roundtrip.animations()) {
        if (original.find_animation(animation.name) == nullptr) {
            comparison.error =
                "roundtrip asset introduced unexpected animation '" + animation.name + "'";
            return comparison;
        }
    }

    return comparison;
}

} // namespace marrow::runtime
