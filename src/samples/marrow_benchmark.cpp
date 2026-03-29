#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "marrow/allocator.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/profiler.hpp"
#include "marrow/runtime/skeleton.hpp"
#include "skeleton_internal.hpp"

namespace {

constexpr std::size_t kDefaultStressSkeletonCount = 100;
constexpr std::size_t kDefaultStressBoneCount = 64;
constexpr std::size_t kDefaultStressFrameCount = 240;
constexpr std::size_t kDefaultStressSampleCount = 5;
constexpr std::size_t kStressWarmupFrameCount = 60;
constexpr double kStressDeltaSeconds = 1.0 / 60.0;
constexpr double kProfilerOverheadBudgetPct = 2.0;
// Very small frame-time deltas are dominated by timer noise on short synthetic runs.
constexpr double kProfilerOverheadBudgetMilliseconds = 0.10;
constexpr double kTargetFrameBudgetMilliseconds = 1000.0 / 60.0;
constexpr std::size_t kTargetMediumSkeletonCount = 200;
constexpr std::size_t kMediumStressBoneCount = 64;

constexpr std::size_t kDefaultSimdBoneCount = 1024;
constexpr std::size_t kDefaultIterations = 40000;
constexpr std::size_t kWarmupIterations = 512;
constexpr float kComparisonTolerance = 1e-5f;

constexpr std::size_t kRuntimeStressFullRateCount = 50;
constexpr std::size_t kRuntimeStressThrottledCount = 50;
constexpr std::size_t kRuntimeStressCulledCount = 100;
constexpr std::size_t kRuntimeStressFrameCount = 600;
constexpr double kRuntimeStressDeltaSeconds = 1.0 / 60.0;
constexpr std::size_t kAnimationLayerWarmupFrameCount = 90;
constexpr std::size_t kAnimationLayerDefaultFrameCount = 360;
constexpr double kAnimationLayerDeltaSeconds = 1.0 / 60.0;

enum class BenchmarkMode {
    StressHarness,
    SimdPropagation,
    RuntimeStress,
    AnimationLayers,
};

enum class StressConstraintDriveMode {
    Animated,
    Idle,
    Partial,
};

struct Options {
    std::size_t skeleton_count{kDefaultStressSkeletonCount};
    std::size_t bone_count{kDefaultStressBoneCount};
    std::size_t frame_count{kDefaultStressFrameCount};
    std::size_t sample_count{kDefaultStressSampleCount};
    std::size_t iterations{kDefaultIterations};
    bool include_clip_attachments{false};
    StressConstraintDriveMode constraint_drive_mode{StressConstraintDriveMode::Animated};
    BenchmarkMode mode{BenchmarkMode::StressHarness};
    std::filesystem::path runtime_stress_skeleton_path;
};

struct TimedResult {
    double milliseconds{0.0};
    double checksum{0.0};
};

struct RuntimeStressInstance {
    explicit RuntimeStressInstance(std::shared_ptr<const marrow::runtime::SkeletonData> data)
        : skeleton(std::move(data)),
          animation_state(skeleton.data()) {}

    marrow::runtime::Skeleton skeleton;
    marrow::runtime::AnimationState animation_state;
};

struct RuntimeStressResult {
    double total_milliseconds{0.0};
    double full_rate_milliseconds{0.0};
    double throttled_milliseconds{0.0};
    double culled_milliseconds{0.0};
    double checksum{0.0};
};

struct AnimationLayerBenchmarkAssets {
    std::shared_ptr<const marrow::runtime::SkeletonData> skeleton_data;
    std::vector<std::size_t> aim_bone_filter;
};

struct AnimationLayerBenchmarkResult {
    double single_layer_milliseconds{0.0};
    double layered_milliseconds{0.0};
    double single_layer_us_per_skeleton{0.0};
    double layered_us_per_skeleton{0.0};
    double overhead_pct{0.0};
    double checksum{0.0};
};

struct LegacyAnimationData;

struct StressBenchmarkAssets {
    std::shared_ptr<const marrow::runtime::SkeletonData> skeleton_data;
    std::shared_ptr<const marrow::runtime::AtlasData> atlas_data;
    std::shared_ptr<LegacyAnimationData> legacy_animation;
    std::vector<std::size_t> mesh_slot_indices;
    std::size_t slot_count{0};
    std::size_t clip_attachment_count{0};
    std::size_t total_mesh_vertices{0};
    std::size_t ik_constraint_count{0};
    std::size_t transform_constraint_count{0};
    std::vector<marrow::runtime::BoneTransform> legacy_setup_poses;
    std::size_t authored_timeline_count{0};
    std::size_t optimized_timeline_count{0};
    std::size_t authored_keyframe_count{0};
    std::size_t optimized_keyframe_count{0};
    std::size_t authored_animation_bytes{0};
    std::size_t optimized_animation_bytes{0};
};

struct StressBenchmarkInstance {
    explicit StressBenchmarkInstance(std::shared_ptr<const marrow::runtime::SkeletonData> data)
        : skeleton(std::move(data)),
          animation_state(skeleton.data()) {}

    marrow::runtime::Skeleton skeleton;
    marrow::runtime::AnimationState animation_state;
    marrow::renderer::PreparedSceneCache render_cache;
};

struct StressSampleMetrics {
    double animation_us{0.0};
    double legacy_animation_us{0.0};
    double transform_us{0.0};
    double skinning_us{0.0};
    double constraint_us{0.0};
    double ik_constraint_solves{0.0};
    double ik_constraint_skips{0.0};
    double transform_constraint_solves{0.0};
    double transform_constraint_skips{0.0};
    double constraint_allocations{0.0};
    double render_us{0.0};
    double frame_ms{0.0};
    double baseline_frame_ms{0.0};
    double profiled_wall_frame_ms{0.0};
    double profiler_overhead_pct{0.0};
    double draw_calls{0.0};
    double streamed_vertices{0.0};
    double batch_merges{0.0};
    double texture_breaks{0.0};
    double blend_breaks{0.0};
    double clip_breaks{0.0};
    double checksum{0.0};
};

struct StressBenchmarkResult {
    std::size_t skeleton_count{0};
    std::size_t bone_count{0};
    std::size_t slot_count{0};
    std::size_t clip_attachment_count{0};
    std::size_t total_mesh_vertices{0};
    std::size_t ik_constraint_count{0};
    std::size_t transform_constraint_count{0};
    double animation_us{0.0};
    double legacy_animation_us{0.0};
    double animation_speedup_pct{0.0};
    double transform_us{0.0};
    double skinning_us{0.0};
    double constraint_us{0.0};
    double ik_constraint_solves{0.0};
    double ik_constraint_skips{0.0};
    double transform_constraint_solves{0.0};
    double transform_constraint_skips{0.0};
    double constraint_allocations{0.0};
    double render_us{0.0};
    double frame_ms{0.0};
    double baseline_frame_ms{0.0};
    double profiled_wall_frame_ms{0.0};
    double profiler_overhead_pct{0.0};
    double draw_calls{0.0};
    double streamed_vertices{0.0};
    double batch_merges{0.0};
    double texture_breaks{0.0};
    double blend_breaks{0.0};
    double clip_breaks{0.0};
    double max_skeletons_at_60fps{0.0};
    double medium_equivalent_max_skeletons_at_60fps{0.0};
    double animation_memory_reduction_pct{0.0};
    double variance_pct{0.0};
    int score{0};
    double checksum{0.0};
    std::size_t authored_timeline_count{0};
    std::size_t optimized_timeline_count{0};
    std::size_t authored_keyframe_count{0};
    std::size_t optimized_keyframe_count{0};
    std::size_t authored_animation_bytes{0};
    std::size_t optimized_animation_bytes{0};
};

struct AnimationStorageStats {
    std::size_t timeline_count{0};
    std::size_t keyframe_count{0};
    std::size_t bytes{0};
};

struct LegacyInterpolation {
    marrow::runtime::InterpolationKind kind{marrow::runtime::InterpolationKind::Linear};
    double cx1{0.0};
    double cy1{0.0};
    double cx2{1.0};
    double cy2{1.0};
};

struct LegacyRotateKeyframe {
    double time{0.0};
    double angle{0.0};
    LegacyInterpolation interpolation{};
};

struct LegacyVectorKeyframe {
    double time{0.0};
    double x{0.0};
    double y{0.0};
    LegacyInterpolation interpolation{};
};

struct LegacyRotateTimeline {
    std::size_t bone_index{0};
    double setup_rotation{0.0};
    std::vector<LegacyRotateKeyframe> keyframes;
};

struct LegacyVectorTimeline {
    std::size_t bone_index{0};
    std::vector<LegacyVectorKeyframe> keyframes;
};

struct LegacySamplingContext {
    double last_sample_time{0.0};
    std::vector<std::size_t> rotate_last_keyframe_indices;
    std::vector<std::size_t> translate_last_keyframe_indices;
    std::vector<std::size_t> scale_last_keyframe_indices;
    std::vector<std::size_t> shear_last_keyframe_indices;

    void prepare(
        std::size_t rotate_count,
        std::size_t translate_count,
        std::size_t scale_count,
        std::size_t shear_count,
        double time_seconds) {
        const bool layout_changed =
            rotate_last_keyframe_indices.size() != rotate_count ||
            translate_last_keyframe_indices.size() != translate_count ||
            scale_last_keyframe_indices.size() != scale_count ||
            shear_last_keyframe_indices.size() != shear_count;
        if (layout_changed) {
            rotate_last_keyframe_indices.assign(rotate_count, 0U);
            translate_last_keyframe_indices.assign(translate_count, 0U);
            scale_last_keyframe_indices.assign(scale_count, 0U);
            shear_last_keyframe_indices.assign(shear_count, 0U);
        } else if (time_seconds + 1e-6 < last_sample_time) {
            std::fill(rotate_last_keyframe_indices.begin(), rotate_last_keyframe_indices.end(), 0U);
            std::fill(
                translate_last_keyframe_indices.begin(),
                translate_last_keyframe_indices.end(),
                0U);
            std::fill(scale_last_keyframe_indices.begin(), scale_last_keyframe_indices.end(), 0U);
            std::fill(shear_last_keyframe_indices.begin(), shear_last_keyframe_indices.end(), 0U);
        }
        last_sample_time = time_seconds;
    }
};

struct LegacyAnimationData {
    std::vector<LegacyRotateTimeline> bone_rotate_timelines;
    std::vector<LegacyVectorTimeline> bone_translate_timelines;
    std::vector<LegacyVectorTimeline> bone_scale_timelines;
    std::vector<LegacyVectorTimeline> bone_shear_timelines;
    double duration{0.0};
};

struct LegacyAnimationInstance {
    explicit LegacyAnimationInstance(const std::vector<marrow::runtime::BoneTransform>& setup_poses)
        : local_poses(setup_poses) {}

    double time_seconds{0.0};
    std::vector<marrow::runtime::BoneTransform> local_poses;
    LegacySamplingContext sampling_context;
};

marrow::runtime::ProfilerDrawStats profiler_draw_stats(
    const marrow::renderer::PreparedSceneBatchSummary& summary) {
    marrow::runtime::ProfilerDrawStats draw_stats;
    draw_stats.skeleton_count = summary.skeleton_count;
    draw_stats.draw_calls = summary.draw_call_count;
    draw_stats.vertices = summary.vertex_count;
    draw_stats.batch_merges = summary.merged_draw_calls;
    draw_stats.break_reasons.texture_changes = summary.break_reasons.texture_changes;
    draw_stats.break_reasons.blend_changes = summary.break_reasons.blend_changes;
    draw_stats.break_reasons.clip_changes = summary.break_reasons.clip_changes;
    draw_stats.break_reasons.shader_changes = summary.break_reasons.shader_changes;
    return draw_stats;
}

LegacyInterpolation to_legacy_interpolation(const marrow::runtime::Interpolation& interpolation) {
    LegacyInterpolation legacy;
    legacy.kind = interpolation.kind();
    if (legacy.kind == marrow::runtime::InterpolationKind::CubicBezier) {
        const auto& bezier = interpolation.cubic_bezier();
        legacy.cx1 = bezier.cx1;
        legacy.cy1 = bezier.cy1;
        legacy.cx2 = bezier.cx2;
        legacy.cy2 = bezier.cy2;
    }
    return legacy;
}

LegacyAnimationData make_legacy_animation(const marrow::runtime::AnimationData& animation) {
    LegacyAnimationData legacy;
    legacy.bone_rotate_timelines.reserve(animation.bone_rotate_timelines.size());
    legacy.bone_translate_timelines.reserve(animation.bone_translate_timelines.size());
    legacy.bone_scale_timelines.reserve(animation.bone_scale_timelines.size());
    legacy.bone_shear_timelines.reserve(animation.bone_shear_timelines.size());

    const auto convert_rotate_timeline = [](const marrow::runtime::BoneRotateTimeline& timeline) {
        LegacyRotateTimeline legacy_timeline;
        legacy_timeline.bone_index = timeline.bone_index;
        legacy_timeline.setup_rotation = timeline.setup_rotation;
        legacy_timeline.keyframes.reserve(timeline.keyframes.size());
        for (const marrow::runtime::RotateKeyframe& keyframe : timeline.keyframes) {
            legacy_timeline.keyframes.push_back(LegacyRotateKeyframe{
                keyframe.time,
                keyframe.angle,
                to_legacy_interpolation(keyframe.interpolation),
            });
        }
        return legacy_timeline;
    };
    const auto convert_vector_timeline = [](const auto& timeline) {
        LegacyVectorTimeline legacy_timeline;
        legacy_timeline.bone_index = timeline.bone_index;
        legacy_timeline.keyframes.reserve(timeline.keyframes.size());
        for (const marrow::runtime::VectorKeyframe& keyframe : timeline.keyframes) {
            legacy_timeline.keyframes.push_back(LegacyVectorKeyframe{
                keyframe.time,
                keyframe.x,
                keyframe.y,
                to_legacy_interpolation(keyframe.interpolation),
            });
        }
        return legacy_timeline;
    };

    for (const auto& timeline : animation.bone_rotate_timelines) {
        legacy.bone_rotate_timelines.push_back(convert_rotate_timeline(timeline));
    }
    for (const auto& timeline : animation.bone_translate_timelines) {
        legacy.bone_translate_timelines.push_back(convert_vector_timeline(timeline));
    }
    for (const auto& timeline : animation.bone_scale_timelines) {
        legacy.bone_scale_timelines.push_back(convert_vector_timeline(timeline));
    }
    for (const auto& timeline : animation.bone_shear_timelines) {
        legacy.bone_shear_timelines.push_back(convert_vector_timeline(timeline));
    }
    legacy.duration = animation.duration();
    return legacy;
}

std::pair<AnimationStorageStats, AnimationStorageStats> accumulate_animation_storage_stats(
    const marrow::runtime::AnimationData& animation) {
    struct LegacyCubicBezierControlPoints {
        double cx1{0.0};
        double cy1{0.0};
        double cx2{1.0};
        double cy2{1.0};
    };
    struct LegacyInterpolationStruct {
        marrow::runtime::InterpolationKind kind{marrow::runtime::InterpolationKind::Linear};
        LegacyCubicBezierControlPoints cubic_bezier{};
    };
    struct LegacyRotateKeyframeStruct {
        double time{0.0};
        double angle{0.0};
        LegacyInterpolationStruct interpolation{};
    };
    struct LegacyVectorKeyframeStruct {
        double time{0.0};
        double x{0.0};
        double y{0.0};
        LegacyInterpolationStruct interpolation{};
    };
    struct LegacyBoneRotateTimelineStruct {
        std::size_t bone_index{0};
        double setup_rotation{0.0};
        std::vector<LegacyRotateKeyframeStruct> keyframes;
    };
    struct LegacyBoneInheritTimelineStruct {
        std::size_t bone_index{0};
        std::vector<marrow::runtime::InheritKeyframe> keyframes;
    };
    struct LegacyBoneVectorTimelineStruct {
        std::size_t bone_index{0};
        std::vector<LegacyVectorKeyframeStruct> keyframes;
    };
    struct LegacyAttachmentKeyframeStruct {
        double time{0.0};
        std::optional<std::string> attachment_name;
    };
    struct LegacySlotAttachmentTimelineStruct {
        std::size_t slot_index{0};
        std::vector<LegacyAttachmentKeyframeStruct> keyframes;
    };
    struct LegacyColorStruct {
        double r{1.0};
        double g{1.0};
        double b{1.0};
        double a{1.0};
    };
    struct LegacyColorKeyframeStruct {
        double time{0.0};
        LegacyColorStruct color{};
        LegacyInterpolationStruct interpolation{};
    };
    struct LegacySlotColorTimelineStruct {
        std::size_t slot_index{0};
        std::vector<LegacyColorKeyframeStruct> keyframes;
    };
    struct LegacyDeformKeyframeStruct {
        double time{0.0};
        std::vector<double> vertex_offsets;
        LegacyInterpolationStruct interpolation{};
    };
    struct LegacyMeshDeformTimelineStruct {
        std::size_t slot_index{0};
        std::string attachment_name;
        std::vector<LegacyDeformKeyframeStruct> keyframes;
    };
    struct LegacyDrawOrderKeyframeStruct {
        double time{0.0};
        std::vector<std::size_t> slot_indices;
    };
    struct LegacyEventKeyframeStruct {
        double time{0.0};
        std::size_t event_index{0};
        std::optional<int> int_value;
        std::optional<double> float_value;
        std::optional<std::string> string_value;
        std::optional<std::string> audio_path;
        std::optional<double> volume;
        std::optional<double> balance;
    };

    AnimationStorageStats stats;
    stats.timeline_count =
        animation.bone_rotate_timelines.size() +
        animation.bone_inherit_timelines.size() +
        animation.bone_translate_timelines.size() +
        animation.bone_scale_timelines.size() +
        animation.bone_shear_timelines.size() +
        animation.slot_attachment_timelines.size() +
        animation.slot_color_timelines.size() +
        animation.mesh_deform_timelines.size() +
        (animation.draw_order_timeline_data.has_value() ? 1U : 0U) +
        (animation.event_timeline_data.has_value() ? 1U : 0U);
    stats.keyframe_count =
        std::accumulate(
            animation.bone_rotate_timelines.begin(),
            animation.bone_rotate_timelines.end(),
            std::size_t{0},
            [](std::size_t total, const auto& timeline) {
                return total + timeline.keyframes.size();
            }) +
        std::accumulate(
            animation.bone_inherit_timelines.begin(),
            animation.bone_inherit_timelines.end(),
            std::size_t{0},
            [](std::size_t total, const auto& timeline) {
                return total + timeline.keyframes.size();
            }) +
        std::accumulate(
            animation.bone_translate_timelines.begin(),
            animation.bone_translate_timelines.end(),
            std::size_t{0},
            [](std::size_t total, const auto& timeline) {
                return total + timeline.keyframes.size();
            }) +
        std::accumulate(
            animation.bone_scale_timelines.begin(),
            animation.bone_scale_timelines.end(),
            std::size_t{0},
            [](std::size_t total, const auto& timeline) {
                return total + timeline.keyframes.size();
            }) +
        std::accumulate(
            animation.bone_shear_timelines.begin(),
            animation.bone_shear_timelines.end(),
            std::size_t{0},
            [](std::size_t total, const auto& timeline) {
                return total + timeline.keyframes.size();
            }) +
        std::accumulate(
            animation.slot_attachment_timelines.begin(),
            animation.slot_attachment_timelines.end(),
            std::size_t{0},
            [](std::size_t total, const auto& timeline) {
                return total + timeline.keyframes.size();
            }) +
        std::accumulate(
            animation.slot_color_timelines.begin(),
            animation.slot_color_timelines.end(),
            std::size_t{0},
            [](std::size_t total, const auto& timeline) {
                return total + timeline.keyframes.size();
            }) +
        std::accumulate(
            animation.mesh_deform_timelines.begin(),
            animation.mesh_deform_timelines.end(),
            std::size_t{0},
            [](std::size_t total, const auto& timeline) {
                return total + timeline.keyframes.size();
            }) +
        (animation.draw_order_timeline_data.has_value()
             ? animation.draw_order_timeline_data->keyframes.size()
             : 0U) +
        (animation.event_timeline_data.has_value()
             ? animation.event_timeline_data->keyframes.size()
             : 0U);

    stats.bytes += animation.bone_rotate_timelines.size() * sizeof(marrow::runtime::BoneRotateTimeline);
    stats.bytes += animation.bone_inherit_timelines.size() * sizeof(marrow::runtime::BoneInheritTimeline);
    stats.bytes += animation.bone_translate_timelines.size() * sizeof(marrow::runtime::BoneTranslateTimeline);
    stats.bytes += animation.bone_scale_timelines.size() * sizeof(marrow::runtime::BoneScaleTimeline);
    stats.bytes += animation.bone_shear_timelines.size() * sizeof(marrow::runtime::BoneShearTimeline);
    stats.bytes += animation.slot_attachment_timelines.size() * sizeof(marrow::runtime::SlotAttachmentTimeline);
    stats.bytes += animation.slot_color_timelines.size() * sizeof(marrow::runtime::SlotColorTimeline);
    stats.bytes += animation.mesh_deform_timelines.size() * sizeof(marrow::runtime::MeshDeformTimeline);
    if (animation.draw_order_timeline_data.has_value()) {
        stats.bytes += sizeof(marrow::runtime::DrawOrderTimeline);
    }
    if (animation.event_timeline_data.has_value()) {
        stats.bytes += sizeof(marrow::runtime::EventTimeline);
    }
    for (const auto& timeline : animation.bone_rotate_timelines) {
        stats.bytes += timeline.keyframes.size() * sizeof(marrow::runtime::RotateKeyframe);
    }
    for (const auto& timeline : animation.bone_inherit_timelines) {
        stats.bytes += timeline.keyframes.size() * sizeof(marrow::runtime::InheritKeyframe);
    }
    for (const auto& timeline : animation.bone_translate_timelines) {
        stats.bytes += timeline.keyframes.size() * sizeof(marrow::runtime::VectorKeyframe);
    }
    for (const auto& timeline : animation.bone_scale_timelines) {
        stats.bytes += timeline.keyframes.size() * sizeof(marrow::runtime::VectorKeyframe);
    }
    for (const auto& timeline : animation.bone_shear_timelines) {
        stats.bytes += timeline.keyframes.size() * sizeof(marrow::runtime::VectorKeyframe);
    }
    for (const auto& timeline : animation.slot_attachment_timelines) {
        stats.bytes += timeline.keyframes.size() * sizeof(marrow::runtime::AttachmentKeyframe);
    }
    for (const auto& timeline : animation.slot_color_timelines) {
        stats.bytes += timeline.keyframes.size() * sizeof(marrow::runtime::ColorKeyframe);
    }
    for (const auto& timeline : animation.mesh_deform_timelines) {
        stats.bytes += timeline.keyframes.size() * sizeof(marrow::runtime::DeformKeyframe);
        for (const auto& keyframe : timeline.keyframes) {
            stats.bytes += keyframe.vertex_offsets.size() * sizeof(marrow::runtime::AnimationScalar);
        }
    }
    if (animation.draw_order_timeline_data.has_value()) {
        stats.bytes +=
            animation.draw_order_timeline_data->keyframes.size() *
            sizeof(marrow::runtime::DrawOrderKeyframe);
    }
    if (animation.event_timeline_data.has_value()) {
        stats.bytes +=
            animation.event_timeline_data->keyframes.size() *
            sizeof(marrow::runtime::EventKeyframe);
    }

    AnimationStorageStats legacy;
    legacy.timeline_count = stats.timeline_count;
    legacy.keyframe_count = stats.keyframe_count;
    legacy.bytes +=
        animation.bone_rotate_timelines.size() * sizeof(LegacyBoneRotateTimelineStruct);
    legacy.bytes +=
        animation.bone_inherit_timelines.size() * sizeof(LegacyBoneInheritTimelineStruct);
    legacy.bytes +=
        (animation.bone_translate_timelines.size() +
         animation.bone_scale_timelines.size() +
         animation.bone_shear_timelines.size()) *
        sizeof(LegacyBoneVectorTimelineStruct);
    legacy.bytes +=
        animation.slot_attachment_timelines.size() * sizeof(LegacySlotAttachmentTimelineStruct);
    legacy.bytes +=
        animation.slot_color_timelines.size() * sizeof(LegacySlotColorTimelineStruct);
    legacy.bytes +=
        animation.mesh_deform_timelines.size() * sizeof(LegacyMeshDeformTimelineStruct);
    if (animation.draw_order_timeline_data.has_value()) {
        legacy.bytes += sizeof(marrow::runtime::DrawOrderTimeline);
    }
    if (animation.event_timeline_data.has_value()) {
        legacy.bytes += sizeof(marrow::runtime::EventTimeline);
    }
    for (const auto& timeline : animation.bone_rotate_timelines) {
        legacy.bytes += timeline.keyframes.size() * sizeof(LegacyRotateKeyframeStruct);
    }
    for (const auto& timeline : animation.bone_inherit_timelines) {
        legacy.bytes += timeline.keyframes.size() * sizeof(marrow::runtime::InheritKeyframe);
    }
    for (const auto& timeline : animation.bone_translate_timelines) {
        legacy.bytes += timeline.keyframes.size() * sizeof(LegacyVectorKeyframeStruct);
    }
    for (const auto& timeline : animation.bone_scale_timelines) {
        legacy.bytes += timeline.keyframes.size() * sizeof(LegacyVectorKeyframeStruct);
    }
    for (const auto& timeline : animation.bone_shear_timelines) {
        legacy.bytes += timeline.keyframes.size() * sizeof(LegacyVectorKeyframeStruct);
    }
    for (const auto& timeline : animation.slot_attachment_timelines) {
        legacy.bytes += timeline.keyframes.size() * sizeof(LegacyAttachmentKeyframeStruct);
    }
    for (const auto& timeline : animation.slot_color_timelines) {
        legacy.bytes += timeline.keyframes.size() * sizeof(LegacyColorKeyframeStruct);
    }
    for (const auto& timeline : animation.mesh_deform_timelines) {
        legacy.bytes += timeline.keyframes.size() * sizeof(LegacyDeformKeyframeStruct);
        for (const auto& keyframe : timeline.keyframes) {
            legacy.bytes += keyframe.vertex_offsets.size() * sizeof(double);
        }
    }
    if (animation.draw_order_timeline_data.has_value()) {
        legacy.bytes +=
            animation.draw_order_timeline_data->keyframes.size() *
            sizeof(LegacyDrawOrderKeyframeStruct);
    }
    if (animation.event_timeline_data.has_value()) {
        legacy.bytes +=
            animation.event_timeline_data->keyframes.size() *
            sizeof(LegacyEventKeyframeStruct);
    }
    return {stats, legacy};
}

double sample_legacy_cubic_component(double control_point_1, double control_point_2, double t) {
    const double inverse_t = 1.0 - t;
    return 3.0 * inverse_t * inverse_t * t * control_point_1 +
        3.0 * inverse_t * t * t * control_point_2 + t * t * t;
}

double sample_legacy_cubic_derivative(double control_point_1, double control_point_2, double t) {
    const double inverse_t = 1.0 - t;
    return 3.0 * inverse_t * inverse_t * control_point_1 +
        6.0 * inverse_t * t * (control_point_2 - control_point_1) +
        3.0 * t * t * (1.0 - control_point_2);
}

double solve_legacy_cubic_bezier_parameter(const LegacyInterpolation& interpolation, double alpha) {
    double t = std::clamp(alpha, 0.0, 1.0);
    for (int iteration = 0; iteration < 8; ++iteration) {
        const double x = sample_legacy_cubic_component(interpolation.cx1, interpolation.cx2, t) - alpha;
        if (std::abs(x) <= 1e-7) {
            return t;
        }
        const double derivative =
            sample_legacy_cubic_derivative(interpolation.cx1, interpolation.cx2, t);
        if (std::abs(derivative) <= 1e-7) {
            break;
        }
        t = std::clamp(t - x / derivative, 0.0, 1.0);
    }

    double lower = 0.0;
    double upper = 1.0;
    t = alpha;
    for (int iteration = 0; iteration < 32; ++iteration) {
        const double x = sample_legacy_cubic_component(interpolation.cx1, interpolation.cx2, t);
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

double transform_legacy_interpolation(const LegacyInterpolation& interpolation, double alpha) {
    alpha = std::clamp(alpha, 0.0, 1.0);
    switch (interpolation.kind) {
    case marrow::runtime::InterpolationKind::Linear:
        return alpha;
    case marrow::runtime::InterpolationKind::Stepped:
        return alpha >= 1.0 ? 1.0 : 0.0;
    case marrow::runtime::InterpolationKind::CubicBezier: {
        const double t = solve_legacy_cubic_bezier_parameter(interpolation, alpha);
        return sample_legacy_cubic_component(interpolation.cy1, interpolation.cy2, t);
    }
    }
    return alpha;
}

double interpolate_legacy_value(
    double from_value,
    double to_value,
    const LegacyInterpolation& interpolation,
    double alpha) {
    const double eased_alpha = transform_legacy_interpolation(interpolation, alpha);
    return from_value + (to_value - from_value) * eased_alpha;
}

template <typename Keyframe>
std::size_t sample_legacy_cursor(
    const std::vector<Keyframe>& keyframes,
    double time_seconds,
    std::size_t* last_keyframe_index,
    double epsilon = 0.0) {
    if (keyframes.empty()) {
        return 0U;
    }
    std::size_t cursor = last_keyframe_index != nullptr ? *last_keyframe_index : 0U;
    cursor = std::min(cursor, keyframes.size() - 1U);
    while (cursor + 1U < keyframes.size() && time_seconds >= keyframes[cursor + 1U].time - epsilon) {
        ++cursor;
    }
    while (cursor > 0U && time_seconds < keyframes[cursor].time - epsilon) {
        --cursor;
    }
    if (last_keyframe_index != nullptr) {
        *last_keyframe_index = cursor;
    }
    return cursor;
}

std::optional<double> sample_legacy_rotate_timeline(
    const LegacyRotateTimeline& timeline,
    double time_seconds,
    std::size_t* last_keyframe_index) {
    if (timeline.keyframes.empty()) {
        return std::nullopt;
    }
    if (timeline.keyframes.size() == 1U || time_seconds <= timeline.keyframes.front().time) {
        if (last_keyframe_index != nullptr) {
            *last_keyframe_index = 0U;
        }
        return timeline.setup_rotation + timeline.keyframes.front().angle;
    }
    const std::size_t cursor =
        sample_legacy_cursor(timeline.keyframes, time_seconds, last_keyframe_index);
    if (cursor + 1U >= timeline.keyframes.size()) {
        return timeline.setup_rotation + timeline.keyframes.back().angle;
    }
    const LegacyRotateKeyframe& previous = timeline.keyframes[cursor];
    const LegacyRotateKeyframe& current = timeline.keyframes[cursor + 1U];
    const double range = current.time - previous.time;
    const double alpha = range > 0.0 ? (time_seconds - previous.time) / range : 0.0;
    return timeline.setup_rotation + interpolate_legacy_value(
        previous.angle,
        current.angle,
        previous.interpolation,
        alpha);
}

std::optional<marrow::runtime::VectorSample> sample_legacy_vector_timeline(
    const LegacyVectorTimeline& timeline,
    double time_seconds,
    std::size_t* last_keyframe_index) {
    if (timeline.keyframes.empty()) {
        return std::nullopt;
    }
    if (timeline.keyframes.size() == 1U || time_seconds <= timeline.keyframes.front().time) {
        if (last_keyframe_index != nullptr) {
            *last_keyframe_index = 0U;
        }
        return marrow::runtime::VectorSample{
            timeline.keyframes.front().x,
            timeline.keyframes.front().y,
        };
    }
    const std::size_t cursor =
        sample_legacy_cursor(timeline.keyframes, time_seconds, last_keyframe_index);
    if (cursor + 1U >= timeline.keyframes.size()) {
        return marrow::runtime::VectorSample{
            timeline.keyframes.back().x,
            timeline.keyframes.back().y,
        };
    }
    const LegacyVectorKeyframe& previous = timeline.keyframes[cursor];
    const LegacyVectorKeyframe& current = timeline.keyframes[cursor + 1U];
    const double range = current.time - previous.time;
    const double alpha = range > 0.0 ? (time_seconds - previous.time) / range : 0.0;
    return marrow::runtime::VectorSample{
        interpolate_legacy_value(previous.x, current.x, previous.interpolation, alpha),
        interpolate_legacy_value(previous.y, current.y, previous.interpolation, alpha),
    };
}

bool parse_size(std::string_view value, std::size_t* out_value) {
    if (out_value == nullptr || value.empty()) {
        return false;
    }

    std::size_t parsed = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9') {
            return false;
        }

        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (parsed > (static_cast<std::size_t>(-1) - digit) / 10U) {
            return false;
        }
        parsed = parsed * 10U + digit;
    }

    *out_value = parsed;
    return true;
}

void print_usage(std::string_view program) {
    std::cerr << "Usage: " << program << '\n'
              << "  " << program
              << " [--skeletons N] [--bones N] [--frames N] [--samples N] [--clips]"
              << " [--constraint-drive animated|idle|partial]\n"
              << "  " << program
              << " --simd-propagation [--bones N] [--iterations N]\n"
              << "  " << program
              << " --animation-layers [--skeletons N] [--bones N] [--frames N]\n"
              << "  " << program
              << " --runtime-stress skeleton.mskl\n";
}

std::string_view constraint_drive_mode_name(StressConstraintDriveMode mode) {
    switch (mode) {
    case StressConstraintDriveMode::Animated:
        return "animated";
    case StressConstraintDriveMode::Idle:
        return "idle";
    case StressConstraintDriveMode::Partial:
        return "partial";
    }

    return "animated";
}

std::optional<StressConstraintDriveMode> parse_constraint_drive_mode(std::string_view mode) {
    if (mode == "animated") {
        return StressConstraintDriveMode::Animated;
    }
    if (mode == "idle") {
        return StressConstraintDriveMode::Idle;
    }
    if (mode == "partial") {
        return StressConstraintDriveMode::Partial;
    }

    return std::nullopt;
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            print_usage(argv[0] != nullptr ? argv[0] : "marrow_benchmark");
            std::exit(0);
        }
        if (argument == "--simd-propagation") {
            options.mode = BenchmarkMode::SimdPropagation;
            continue;
        }
        if (argument == "--clips") {
            options.include_clip_attachments = true;
            continue;
        }
        if (argument == "--animation-layers") {
            options.mode = BenchmarkMode::AnimationLayers;
            if (options.frame_count == kDefaultStressFrameCount) {
                options.frame_count = kAnimationLayerDefaultFrameCount;
            }
            continue;
        }
        if (argument == "--runtime-stress") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("missing skeleton path for --runtime-stress");
            }
            options.mode = BenchmarkMode::RuntimeStress;
            options.runtime_stress_skeleton_path = argv[++index];
            continue;
        }
        if (argument == "--constraint-drive") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("missing mode for --constraint-drive");
            }

            const auto mode = parse_constraint_drive_mode(argv[index + 1]);
            if (!mode.has_value()) {
                throw std::invalid_argument(
                    "invalid value for --constraint-drive (expected animated, idle, or partial)");
            }
            options.constraint_drive_mode = *mode;
            ++index;
            continue;
        }
        if (argument == "--bones" ||
            argument == "--iterations" ||
            argument == "--skeletons" ||
            argument == "--frames" ||
            argument == "--samples") {
            if (index + 1 >= argc) {
                throw std::invalid_argument("missing value for " + std::string(argument));
            }

            std::size_t value = 0;
            if (!parse_size(argv[index + 1], &value) || value == 0U) {
                throw std::invalid_argument("invalid numeric value for " + std::string(argument));
            }

            if (argument == "--bones") {
                if (value < 2U) {
                    throw std::invalid_argument("--bones requires a value of at least 2");
                }
                options.bone_count = value;
            } else if (argument == "--iterations") {
                options.iterations = value;
            } else if (argument == "--skeletons") {
                options.skeleton_count = value;
            } else if (argument == "--frames") {
                options.frame_count = value;
            } else {
                options.sample_count = value;
            }

            ++index;
            continue;
        }

        throw std::invalid_argument("unknown option " + std::string(argument));
    }

    return options;
}

template <typename Fn>
double measure_seconds(Fn&& fn) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    fn();
    return std::chrono::duration<double>(clock::now() - start).count();
}

std::size_t round_up_even(std::size_t value) {
    return (value % 2U) == 0U ? value : value + 1U;
}

std::vector<marrow::runtime::BoneData> make_simd_benchmark_bones(std::size_t bone_count) {
    using marrow::runtime::BoneData;
    using marrow::runtime::BoneTransform;

    std::vector<BoneData> bones;
    bones.reserve(bone_count);

    BoneData root;
    root.name = "root";
    bones.push_back(std::move(root));

    std::size_t next_parent = 0;
    while (bones.size() < bone_count) {
        const std::size_t parent_index = next_parent++;
        for (std::size_t sibling = 0; sibling < 4U && bones.size() < bone_count; ++sibling) {
            const std::size_t bone_index = bones.size();
            BoneData bone;
            bone.name = "bone_" + std::to_string(bone_index);
            bone.parent_index = parent_index;

            const double phase = static_cast<double>(bone_index % 37U);
            bone.setup_pose = BoneTransform{
                1.0 + static_cast<double>(sibling) * 0.35 + phase * 0.015,
                -0.75 + static_cast<double>(bone_index % 11U) * 0.175,
                std::fmod(static_cast<double>(bone_index * 11U), 360.0),
                0.85 + static_cast<double>(bone_index % 7U) * 0.05,
                0.9 + static_cast<double>(bone_index % 5U) * 0.06,
                -6.0 + static_cast<double>(bone_index % 9U) * 1.5,
                -4.0 + static_cast<double>(bone_index % 13U) * 0.9,
            };
            bones.push_back(std::move(bone));
        }
    }

    return bones;
}

std::vector<marrow::runtime::BonePose> make_simd_benchmark_poses(
    const std::vector<marrow::runtime::BoneData>& bones) {
    std::vector<marrow::runtime::BonePose> poses;
    poses.reserve(bones.size());
    for (const marrow::runtime::BoneData& bone : bones) {
        poses.push_back(marrow::runtime::BonePose{bone.setup_pose, bone.inherit});
    }
    return poses;
}

bool compare_buffers(
    const std::vector<float>& lhs,
    const std::vector<float>& rhs,
    std::string_view label) {
    if (lhs.size() != rhs.size()) {
        std::cerr << label << " size mismatch.\n";
        return false;
    }

    for (std::size_t index = 0; index < lhs.size(); ++index) {
        if (std::abs(lhs[index] - rhs[index]) <= kComparisonTolerance) {
            continue;
        }

        std::cerr << label << " mismatch at bone " << index << ": "
                  << lhs[index] << " vs " << rhs[index] << '\n';
        return false;
    }

    return true;
}

template <typename Fn>
TimedResult time_iterations(std::size_t iterations, Fn&& fn) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    double checksum = 0.0;
    for (std::size_t iteration = 0; iteration < iterations; ++iteration) {
        fn();
        checksum += static_cast<double>(iteration % 17U);
    }
    const auto end = clock::now();
    const std::chrono::duration<double, std::milli> elapsed = end - start;
    return {elapsed.count(), checksum};
}

std::vector<RuntimeStressInstance> make_runtime_stress_instances(
    const std::shared_ptr<const marrow::runtime::SkeletonData>& data,
    std::size_t count,
    std::string_view animation_name,
    bool loop,
    std::size_t update_interval,
    bool visible) {
    std::vector<RuntimeStressInstance> instances;
    instances.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        instances.emplace_back(data);
        RuntimeStressInstance& instance = instances.back();
        instance.skeleton.set_update_interval(update_interval);
        instance.skeleton.set_visible(visible);
        instance.animation_state.set_animation(0, animation_name, loop);
    }
    return instances;
}

template <typename Clock>
double elapsed_milliseconds(const typename Clock::time_point& start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

RuntimeStressResult run_runtime_stress_benchmark(const Options& options) {
    if (options.runtime_stress_skeleton_path.empty()) {
        throw std::invalid_argument("--runtime-stress requires a skeleton path");
    }

    const auto skeleton_result =
        marrow::runtime::load_skeleton_data(options.runtime_stress_skeleton_path);
    if (!skeleton_result) {
        throw std::runtime_error(skeleton_result.error->format());
    }

    const std::shared_ptr<const marrow::runtime::SkeletonData>& data =
        skeleton_result.skeleton_data;
    const marrow::runtime::AnimationData* animation = data->find_animation("idle");
    if (animation == nullptr) {
        if (data->animations().empty()) {
            throw std::runtime_error("runtime stress benchmark requires at least one animation");
        }
        animation = &data->animations().front();
    }

    const std::string animation_name = animation->name;
    auto full_rate_instances = make_runtime_stress_instances(
        data,
        kRuntimeStressFullRateCount,
        animation_name,
        true,
        1U,
        true);
    auto throttled_instances = make_runtime_stress_instances(
        data,
        kRuntimeStressThrottledCount,
        animation_name,
        true,
        3U,
        true);
    auto culled_instances = make_runtime_stress_instances(
        data,
        kRuntimeStressCulledCount,
        animation_name,
        true,
        1U,
        false);

    RuntimeStressResult result;
    using clock = std::chrono::steady_clock;
    const auto total_start = clock::now();
    for (std::size_t frame = 0; frame < kRuntimeStressFrameCount; ++frame) {
        const auto full_start = clock::now();
        for (RuntimeStressInstance& instance : full_rate_instances) {
            marrow::runtime::update_instance(
                instance.skeleton,
                instance.animation_state,
                kRuntimeStressDeltaSeconds);
            if (!instance.skeleton.bone_world_transforms().empty()) {
                result.checksum += instance.skeleton.bone_world_transforms().front().world_x;
            }
        }
        result.full_rate_milliseconds += elapsed_milliseconds<clock>(full_start);

        const auto throttled_start = clock::now();
        for (RuntimeStressInstance& instance : throttled_instances) {
            marrow::runtime::update_instance(
                instance.skeleton,
                instance.animation_state,
                kRuntimeStressDeltaSeconds);
            if (!instance.skeleton.bone_world_transforms().empty()) {
                result.checksum += instance.skeleton.bone_world_transforms().front().world_x;
            }
        }
        result.throttled_milliseconds += elapsed_milliseconds<clock>(throttled_start);

        const auto culled_start = clock::now();
        for (RuntimeStressInstance& instance : culled_instances) {
            marrow::runtime::update_instance(
                instance.skeleton,
                instance.animation_state,
                kRuntimeStressDeltaSeconds);
            if (!instance.skeleton.bone_world_transforms().empty()) {
                result.checksum += instance.skeleton.bone_world_transforms().front().world_x;
            }
        }
        result.culled_milliseconds += elapsed_milliseconds<clock>(culled_start);
    }

    result.total_milliseconds = elapsed_milliseconds<clock>(total_start);
    return result;
}

std::vector<marrow::runtime::BoneData> make_stress_bones(std::size_t bone_count);

std::vector<std::size_t> collect_layer_bone_indices(
    std::size_t bone_count,
    std::size_t start_index,
    std::size_t stride,
    std::size_t max_count) {
    std::vector<std::size_t> indices;
    if (bone_count == 0U || max_count == 0U) {
        return indices;
    }

    const std::size_t safe_start = std::min(start_index, bone_count - 1U);
    const std::size_t safe_stride = std::max<std::size_t>(1U, stride);
    for (std::size_t bone_index = safe_start;
         bone_index < bone_count && indices.size() < max_count;
         bone_index += safe_stride) {
        indices.push_back(bone_index);
    }

    if (indices.empty()) {
        indices.push_back(safe_start);
    }
    return indices;
}

marrow::runtime::AnimationData make_layer_benchmark_animation(
    std::string name,
    const std::vector<marrow::runtime::BoneData>& bones,
    const std::vector<std::size_t>& targeted_bones,
    double rotate_delta,
    double translate_delta) {
    using marrow::runtime::AnimationData;
    using marrow::runtime::BoneRotateTimeline;
    using marrow::runtime::BoneTranslateTimeline;
    using marrow::runtime::Interpolation;
    using marrow::runtime::RotateKeyframe;
    using marrow::runtime::VectorKeyframe;

    AnimationData animation;
    animation.name = std::move(name);
    animation.targeted_bone_indices = targeted_bones;

    for (std::size_t order = 0; order < targeted_bones.size(); ++order) {
        const std::size_t bone_index = targeted_bones[order];
        if (bone_index >= bones.size()) {
            continue;
        }

        const marrow::runtime::BoneTransform& setup = bones[bone_index].setup_pose;
        const double phase = static_cast<double>(order % 5U) * 0.35;

        BoneRotateTimeline rotate;
        rotate.bone_index = bone_index;
        rotate.setup_rotation = setup.rotation;
        rotate.keyframes = {
            RotateKeyframe{
                0.0,
                -rotate_delta * 0.45 + phase,
                Interpolation::linear(),
            },
            RotateKeyframe{
                0.5,
                rotate_delta + phase * 0.5,
                Interpolation::linear(),
            },
            RotateKeyframe{
                1.0,
                -rotate_delta * 0.35,
                Interpolation::linear(),
            },
        };
        animation.bone_rotate_timelines.push_back(std::move(rotate));

        if (translate_delta <= 0.0 || (order % 2U) != 0U) {
            continue;
        }

        BoneTranslateTimeline translate;
        translate.bone_index = bone_index;
        translate.keyframes = {
            VectorKeyframe{0.0, setup.x, setup.y, Interpolation::linear()},
            VectorKeyframe{
                0.5,
                setup.x + translate_delta,
                setup.y + translate_delta * ((order % 4U) < 2U ? 0.5 : -0.5),
                Interpolation::linear(),
            },
            VectorKeyframe{1.0, setup.x, setup.y, Interpolation::linear()},
        };
        animation.bone_translate_timelines.push_back(std::move(translate));
    }

    return animation;
}

AnimationLayerBenchmarkAssets make_animation_layer_benchmark_assets(const Options& options) {
    std::vector<marrow::runtime::BoneData> bones = make_stress_bones(options.bone_count);
    const std::vector<std::size_t> walk_bones =
        collect_layer_bone_indices(bones.size(), 0U, 1U, bones.size());
    const std::vector<std::size_t> breathing_bones =
        collect_layer_bone_indices(
            bones.size(),
            1U,
            4U,
            std::max<std::size_t>(4U, bones.size() / 8U));
    const std::vector<std::size_t> aim_bones =
        collect_layer_bone_indices(
            bones.size(),
            std::max<std::size_t>(1U, bones.size() / 2U),
            2U,
            std::max<std::size_t>(6U, bones.size() / 10U));

    marrow::runtime::SkeletonInfo info;
    info.name = "animation_layer_benchmark";

    std::vector<marrow::runtime::AnimationData> animations;
    animations.push_back(
        make_layer_benchmark_animation("walk", bones, walk_bones, 8.0, 1.5));
    animations.push_back(
        make_layer_benchmark_animation("breathing", bones, breathing_bones, 3.0, 0.75));
    animations.push_back(
        make_layer_benchmark_animation("aim", bones, aim_bones, 18.0, 2.0));

    AnimationLayerBenchmarkAssets assets;
    assets.aim_bone_filter = aim_bones;
    assets.skeleton_data = marrow::allocate_shared<marrow::runtime::SkeletonData>(
        std::move(info),
        std::move(bones),
        std::vector<marrow::runtime::IkConstraintData>{},
        std::vector<marrow::runtime::PathConstraintData>{},
        std::vector<marrow::runtime::TransformConstraintData>{},
        std::vector<marrow::runtime::PhysicsConstraintData>{},
        std::vector<marrow::runtime::SlotData>{},
        std::vector<marrow::runtime::EventDefinition>{},
        std::move(animations),
        std::vector<marrow::runtime::SkinData>{},
        0.0,
        std::vector<marrow::runtime::AnimationMixDefinition>{});
    return assets;
}

std::vector<marrow::runtime::BoneData> make_stress_bones(std::size_t bone_count) {
    std::vector<marrow::runtime::BoneData> bones;
    bones.reserve(bone_count);

    marrow::runtime::BoneData root;
    root.name = "root";
    bones.push_back(std::move(root));

    for (std::size_t bone_index = 1; bone_index < bone_count; ++bone_index) {
        marrow::runtime::BoneData bone;
        bone.name = "bone_" + std::to_string(bone_index);
        bone.parent_index = bone_index - 1U;
        bone.setup_pose.x = 12.0 + static_cast<double>(bone_index % 3U) * 1.75;
        bone.setup_pose.y = (bone_index % 2U == 0U) ? 2.0 : -2.0;
        bone.setup_pose.rotation =
            static_cast<double>(static_cast<int>(bone_index % 7U) - 3) * 4.0;
        bone.setup_pose.scale_x = 1.0 + static_cast<double>(bone_index % 5U) * 0.015;
        bone.setup_pose.scale_y = 1.0 - static_cast<double>(bone_index % 4U) * 0.01;
        bone.setup_pose.shear_x = static_cast<double>(bone_index % 3U) * 0.5;
        bone.setup_pose.shear_y = static_cast<double>(bone_index % 2U) * -0.35;
        bones.push_back(std::move(bone));
    }

    return bones;
}

std::shared_ptr<const marrow::runtime::MeshGeometry> make_stress_mesh_geometry(
    std::size_t base_bone_index,
    std::size_t bone_count,
    std::size_t vertex_count) {
    if (bone_count == 0U) {
        throw std::invalid_argument("stress mesh geometry requires at least one bone");
    }

    marrow::runtime::MeshGeometry geometry;
    const std::size_t safe_vertex_count = std::max<std::size_t>(4U, round_up_even(vertex_count));
    const std::size_t columns = safe_vertex_count / 2U;
    const std::size_t max_bone_index = bone_count - 1U;
    const std::size_t clamped_base_bone = std::min(base_bone_index, max_bone_index);
    const std::size_t progression_divisor = std::max<std::size_t>(1U, columns / 4U);
    constexpr std::array<double, 4> kInfluenceWeights{{0.5, 0.25, 0.15, 0.10}};

    geometry.vertices.reserve(safe_vertex_count * 2U);
    geometry.uvs.reserve(safe_vertex_count * 2U);
    geometry.weights.reserve(safe_vertex_count);
    geometry.triangles.reserve((columns - 1U) * 6U);

    for (std::size_t column = 0; column < columns; ++column) {
        const double x = static_cast<double>(column) * 14.0;
        const double u = columns > 1U
            ? static_cast<double>(column) / static_cast<double>(columns - 1U)
            : 0.0;
        const std::size_t progression = column / progression_divisor;
        const std::size_t influence_root = std::min(clamped_base_bone + progression, max_bone_index);

        for (std::size_t row = 0; row < 2U; ++row) {
            const double y = row == 0U ? -18.0 : 18.0;
            geometry.vertices.push_back(x);
            geometry.vertices.push_back(y);
            geometry.uvs.push_back(u);
            geometry.uvs.push_back(row == 0U ? 0.0 : 1.0);

            marrow::runtime::MeshGeometry::VertexWeights weights;
            weights.influences.reserve(kInfluenceWeights.size());
            for (std::size_t influence_index = 0;
                 influence_index < kInfluenceWeights.size();
                 ++influence_index) {
                const std::size_t bone_index =
                    std::min(influence_root + influence_index, max_bone_index);
                const double local_x = x - static_cast<double>(influence_index) * 4.5;
                const double local_y =
                    y + static_cast<double>(influence_index) * (row == 0U ? -1.25 : 1.25);
                weights.influences.push_back({
                    bone_index,
                    local_x,
                    local_y,
                    kInfluenceWeights[influence_index],
                });
            }
            geometry.weights.push_back(std::move(weights));
        }
    }

    for (std::size_t column = 0; column + 1U < columns; ++column) {
        const std::size_t base = column * 2U;
        geometry.triangles.push_back(base + 0U);
        geometry.triangles.push_back(base + 2U);
        geometry.triangles.push_back(base + 1U);
        geometry.triangles.push_back(base + 1U);
        geometry.triangles.push_back(base + 2U);
        geometry.triangles.push_back(base + 3U);
    }

    return marrow::allocate_shared<marrow::runtime::MeshGeometry>(std::move(geometry));
}

marrow::runtime::AnimationData make_stress_animation(
    const std::vector<marrow::runtime::BoneData>& bones,
    StressConstraintDriveMode drive_mode) {
    using marrow::runtime::AnimationData;
    using marrow::runtime::BoneRotateTimeline;
    using marrow::runtime::BoneScaleTimeline;
    using marrow::runtime::BoneShearTimeline;
    using marrow::runtime::BoneTranslateTimeline;
    using marrow::runtime::Interpolation;
    using marrow::runtime::RotateKeyframe;
    using marrow::runtime::VectorKeyframe;

    AnimationData animation;
    animation.name = "benchmark";
    animation.targeted_bone_indices.reserve(bones.size());
    animation.bone_rotate_timelines.reserve(bones.size());
    animation.bone_translate_timelines.reserve(bones.size());
    const std::size_t constraint_group_count = (bones.size() + 11U) / 12U;

    for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
        animation.targeted_bone_indices.push_back(bone_index);
        const marrow::runtime::BoneTransform& setup = bones[bone_index].setup_pose;
        const Interpolation stress_curve = Interpolation::cubic_bezier(0.25, 0.10, 0.75, 0.90);
        const std::size_t constraint_group = bone_index / 12U;
        const bool animate_group =
            drive_mode == StressConstraintDriveMode::Animated ||
            (drive_mode == StressConstraintDriveMode::Partial &&
             constraint_group_count > 0U &&
             constraint_group >= constraint_group_count / 2U);

        BoneRotateTimeline rotate;
        rotate.bone_index = bone_index;
        rotate.setup_rotation = setup.rotation;
        if (animate_group && (bone_index % 2U) == 0U) {
            rotate.keyframes = {
                RotateKeyframe{
                    0.0,
                    -6.0 - static_cast<double>(bone_index % 3U),
                    stress_curve,
                },
                RotateKeyframe{
                    0.5,
                    8.0 + static_cast<double>(bone_index % 5U),
                    Interpolation::linear(),
                },
                RotateKeyframe{
                    1.0,
                    -4.0,
                    Interpolation::linear(),
                },
            };
        } else {
            rotate.keyframes = {
                RotateKeyframe{0.0, 0.0, Interpolation::linear()},
            };
        }
        animation.bone_rotate_timelines.push_back(std::move(rotate));

        BoneTranslateTimeline translate;
        translate.bone_index = bone_index;
        if (animate_group && (bone_index % 3U) == 0U) {
            translate.keyframes = {
                VectorKeyframe{0.0, setup.x - 1.0, setup.y, stress_curve},
                VectorKeyframe{
                    0.5,
                    setup.x + 2.5 + static_cast<double>(bone_index % 4U) * 0.25,
                    setup.y + (bone_index % 2U == 0U ? 1.25 : -1.25),
                    Interpolation::linear(),
                },
                VectorKeyframe{1.0, setup.x - 0.75, setup.y, Interpolation::linear()},
            };
        } else {
            translate.keyframes = {
                VectorKeyframe{0.0, setup.x, setup.y, Interpolation::linear()},
            };
        }
        animation.bone_translate_timelines.push_back(std::move(translate));

        BoneScaleTimeline scale;
        scale.bone_index = bone_index;
        if (animate_group && (bone_index % 6U) == 0U) {
            scale.keyframes = {
                VectorKeyframe{0.0, setup.scale_x, setup.scale_y, stress_curve},
                VectorKeyframe{
                    0.5,
                    setup.scale_x * 1.05,
                    setup.scale_y * 0.97,
                    Interpolation::linear(),
                },
                VectorKeyframe{1.0, setup.scale_x, setup.scale_y, Interpolation::linear()},
            };
        } else {
            scale.keyframes = {
                VectorKeyframe{0.0, setup.scale_x, setup.scale_y, Interpolation::linear()},
            };
        }
        animation.bone_scale_timelines.push_back(std::move(scale));

        BoneShearTimeline shear;
        shear.bone_index = bone_index;
        if (animate_group && (bone_index % 10U) == 0U) {
            shear.keyframes = {
                VectorKeyframe{0.0, setup.shear_x, setup.shear_y, stress_curve},
                VectorKeyframe{
                    0.5,
                    setup.shear_x + 2.0,
                    setup.shear_y - 1.0,
                    Interpolation::linear(),
                },
                VectorKeyframe{1.0, setup.shear_x, setup.shear_y, Interpolation::linear()},
            };
        } else {
            shear.keyframes = {
                VectorKeyframe{0.0, setup.shear_x, setup.shear_y, Interpolation::linear()},
            };
        }
        animation.bone_shear_timelines.push_back(std::move(shear));
    }

    return animation;
}

StressBenchmarkAssets make_stress_benchmark_assets(const Options& options) {
    if (options.bone_count < 4U) {
        throw std::invalid_argument("stress benchmark requires at least 4 bones");
    }

    std::vector<marrow::runtime::BoneData> bones = make_stress_bones(options.bone_count);
    const std::size_t mesh_slot_count = std::max<std::size_t>(1U, options.bone_count / 8U);
    const std::size_t clip_attachment_count = options.include_clip_attachments ? 1U : 0U;
    const std::size_t slot_count = mesh_slot_count + clip_attachment_count;
    const std::size_t total_mesh_vertices = std::max<std::size_t>(32U, options.bone_count * 4U);
    const std::size_t vertices_per_slot = round_up_even(std::max<std::size_t>(
        4U,
        total_mesh_vertices / mesh_slot_count));
    const std::size_t bones_per_slot =
        std::max<std::size_t>(1U, options.bone_count / mesh_slot_count);
    const std::size_t atlas_columns = std::max<std::size_t>(
        1U,
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(mesh_slot_count)))));
    const std::size_t atlas_rows = (mesh_slot_count + atlas_columns - 1U) / atlas_columns;

    std::vector<marrow::runtime::IkConstraintData> ik_constraints;
    for (std::size_t base = 0; base + 3U < options.bone_count; base += 12U) {
        marrow::runtime::IkConstraintData constraint;
        constraint.name = "ik_" + std::to_string(ik_constraints.size());
        constraint.bone_indices = {base + 1U, base + 2U};
        constraint.target_bone_index = base + 3U;
        constraint.mix = 0.45;
        constraint.softness = 2.0;
        constraint.bend_positive = ((base / 12U) % 2U) == 0U;
        ik_constraints.push_back(std::move(constraint));
    }

    std::vector<marrow::runtime::TransformConstraintData> transform_constraints;
    for (std::size_t source = 4U; source + 2U < options.bone_count; source += 12U) {
        marrow::runtime::TransformConstraintData constraint;
        constraint.name = "transform_" + std::to_string(transform_constraints.size());
        constraint.source_bone_index = source;
        constraint.target_bone_indices = {source + 1U, source + 2U};
        constraint.rotate_mix = 0.25;
        constraint.translate_mix = 0.30;
        constraint.scale_mix = 0.15;
        constraint.shear_mix = 0.10;
        constraint.offsets.rotation = (source % 2U == 0U) ? 7.0 : -7.0;
        constraint.offsets.x = 1.5;
        constraint.offsets.y = -0.5;
        transform_constraints.push_back(std::move(constraint));
    }

    std::vector<marrow::runtime::SlotData> slots;
    std::vector<marrow::runtime::SkinSlotData> slot_attachments;
    std::vector<marrow::runtime::AtlasRegion> regions;
    std::vector<std::size_t> mesh_slot_indices;
    slots.reserve(slot_count);
    slot_attachments.reserve(slot_count);
    regions.reserve(mesh_slot_count);
    mesh_slot_indices.reserve(mesh_slot_count);

    if (options.include_clip_attachments) {
        marrow::runtime::SlotData clip_slot;
        clip_slot.name = "clip_0";
        clip_slot.bone_index = 0U;
        clip_slot.setup_attachment = "clip_mask_0";
        slots.push_back(clip_slot);

        marrow::runtime::AttachmentData clip_attachment;
        clip_attachment.name = "clip_mask_0";
        clip_attachment.kind = marrow::runtime::AttachmentKind::Clipping;
        clip_attachment.clipping_attachment = marrow::runtime::ClippingAttachmentData{
            {
                marrow::runtime::AttachmentVertex{-4096.0, -4096.0},
                marrow::runtime::AttachmentVertex{4096.0, -4096.0},
                marrow::runtime::AttachmentVertex{4096.0, 4096.0},
                marrow::runtime::AttachmentVertex{-4096.0, 4096.0},
            },
            mesh_slot_count > 0U ? std::optional<std::size_t>{1U} : std::nullopt,
            mesh_slot_count > 0U ? std::string{"slot_0"} : std::string{},
        };
        slot_attachments.push_back({0U, std::move(clip_attachment)});
    }

    const std::size_t mesh_slot_offset = clip_attachment_count;
    for (std::size_t mesh_slot_index = 0; mesh_slot_index < mesh_slot_count; ++mesh_slot_index) {
        const std::size_t slot_index = mesh_slot_index + mesh_slot_offset;
        const std::size_t desired_bone =
            std::min<std::size_t>(
                1U + mesh_slot_index * bones_per_slot,
                options.bone_count - 1U);
        const std::size_t bone_index = std::min<std::size_t>(
            desired_bone,
            options.bone_count > 4U ? options.bone_count - 4U : options.bone_count - 1U);
        const std::string attachment_name = "mesh_" + std::to_string(mesh_slot_index);

        marrow::runtime::SlotData slot;
        slot.name = "slot_" + std::to_string(mesh_slot_index);
        slot.bone_index = bone_index;
        slot.setup_attachment = attachment_name;
        slot.color = marrow::runtime::SlotColor{
            1.0,
            0.95 - static_cast<double>(mesh_slot_index % 3U) * 0.04,
            0.92 - static_cast<double>(mesh_slot_index % 4U) * 0.03,
            1.0,
        };
        slots.push_back(slot);

        marrow::runtime::AttachmentData attachment;
        attachment.name = attachment_name;
        attachment.kind = marrow::runtime::AttachmentKind::Mesh;
        attachment.region_name = attachment_name;
        attachment.mesh_geometry = make_stress_mesh_geometry(
            bone_index,
            options.bone_count,
            vertices_per_slot);
        slot_attachments.push_back({
            slot_index,
            std::move(attachment),
        });

        marrow::runtime::AtlasRegion region;
        region.name = attachment_name;
        region.x = static_cast<double>((mesh_slot_index % atlas_columns) * 128U);
        region.y = static_cast<double>((mesh_slot_index / atlas_columns) * 128U);
        region.width = 96.0;
        region.height = 96.0;
        region.origin_x = 48.0;
        region.origin_y = 48.0;
        regions.push_back(std::move(region));

        mesh_slot_indices.push_back(slot_index);
    }

    marrow::runtime::SkinData default_skin;
    default_skin.name = "default";
    default_skin.slot_attachments = std::move(slot_attachments);

    std::vector<marrow::runtime::BoneTransform> legacy_setup_poses;
    legacy_setup_poses.reserve(bones.size());
    for (const marrow::runtime::BoneData& bone : bones) {
        legacy_setup_poses.push_back(bone.setup_pose);
    }

    marrow::runtime::AnimationData animation =
        make_stress_animation(bones, options.constraint_drive_mode);
    const auto authored_storage = accumulate_animation_storage_stats(animation);
    LegacyAnimationData legacy_animation = make_legacy_animation(animation);
    marrow::runtime::SkeletonInfo info;
    info.name = "benchmark_medium";
    info.width = static_cast<double>(mesh_slot_count) * 96.0;
    info.height = 256.0;

    marrow::runtime::AtlasInfo atlas_info;
    atlas_info.name = "benchmark_medium_atlas";
    atlas_info.image = "benchmark_medium.png";
    atlas_info.width = static_cast<double>(atlas_columns * 128U);
    atlas_info.height = static_cast<double>(atlas_rows * 128U);
    atlas_info.filter_min = "linear";
    atlas_info.filter_mag = "linear";
    atlas_info.wrap_x = "clamp";
    atlas_info.wrap_y = "clamp";

    StressBenchmarkAssets assets;
    assets.skeleton_data = marrow::allocate_shared<marrow::runtime::SkeletonData>(
        std::move(info),
        std::move(bones),
        std::move(ik_constraints),
        std::vector<marrow::runtime::PathConstraintData>{},
        std::move(transform_constraints),
        std::vector<marrow::runtime::PhysicsConstraintData>{},
        std::move(slots),
        std::vector<marrow::runtime::EventDefinition>{},
        std::vector<marrow::runtime::AnimationData>{std::move(animation)},
        std::vector<marrow::runtime::SkinData>{std::move(default_skin)},
        0.0,
        std::vector<marrow::runtime::AnimationMixDefinition>{});
    assets.atlas_data = marrow::allocate_shared<marrow::runtime::AtlasData>(
        std::move(atlas_info),
        std::move(regions));
    assets.legacy_animation =
        std::make_shared<LegacyAnimationData>(std::move(legacy_animation));
    assets.legacy_setup_poses = std::move(legacy_setup_poses);
    assets.mesh_slot_indices = std::move(mesh_slot_indices);
    assets.slot_count = slot_count;
    assets.clip_attachment_count = clip_attachment_count;
    assets.total_mesh_vertices = mesh_slot_count * vertices_per_slot;
    assets.ik_constraint_count = assets.skeleton_data->ik_constraints().size();
    assets.transform_constraint_count = assets.skeleton_data->transform_constraints().size();
    if (!assets.skeleton_data->animations().empty()) {
        const auto optimized_storage =
            accumulate_animation_storage_stats(assets.skeleton_data->animations().front());
        assets.authored_timeline_count = authored_storage.first.timeline_count;
        assets.authored_keyframe_count = authored_storage.first.keyframe_count;
        assets.authored_animation_bytes = authored_storage.second.bytes;
        assets.optimized_timeline_count = optimized_storage.first.timeline_count;
        assets.optimized_keyframe_count = optimized_storage.first.keyframe_count;
        assets.optimized_animation_bytes = optimized_storage.first.bytes;
    }
    return assets;
}

std::vector<StressBenchmarkInstance> make_stress_instances(
    const StressBenchmarkAssets& assets,
    std::size_t skeleton_count) {
    std::vector<StressBenchmarkInstance> instances;
    instances.reserve(skeleton_count);
    for (std::size_t skeleton_index = 0; skeleton_index < skeleton_count; ++skeleton_index) {
        instances.emplace_back(assets.skeleton_data);
        StressBenchmarkInstance& instance = instances.back();
        instance.animation_state.set_animation(0, "benchmark", true);
        instance.skeleton.set_attachment_playback_time(
            static_cast<double>(skeleton_index % 7U) * 0.03125);
    }
    return instances;
}

std::vector<LegacyAnimationInstance> make_legacy_animation_instances(
    const StressBenchmarkAssets& assets,
    std::size_t skeleton_count) {
    std::vector<LegacyAnimationInstance> instances;
    instances.reserve(skeleton_count);
    for (std::size_t skeleton_index = 0; skeleton_index < skeleton_count; ++skeleton_index) {
        instances.emplace_back(assets.legacy_setup_poses);
        instances.back().time_seconds =
            static_cast<double>(skeleton_index % 7U) * 0.03125;
    }
    return instances;
}

void run_legacy_animation_frame(
    std::vector<LegacyAnimationInstance>* instances,
    const StressBenchmarkAssets& assets,
    double* checksum_out) {
    if (instances == nullptr) {
        return;
    }

    if (assets.legacy_animation == nullptr) {
        return;
    }
    const LegacyAnimationData& animation = *assets.legacy_animation;
    for (LegacyAnimationInstance& instance : *instances) {
        if (!assets.legacy_setup_poses.empty()) {
            instance.local_poses = assets.legacy_setup_poses;
        }

        if (animation.duration > 0.0) {
            instance.time_seconds = std::fmod(
                instance.time_seconds + kStressDeltaSeconds,
                animation.duration);
        } else {
            instance.time_seconds = 0.0;
        }

        instance.sampling_context.prepare(
            animation.bone_rotate_timelines.size(),
            animation.bone_translate_timelines.size(),
            animation.bone_scale_timelines.size(),
            animation.bone_shear_timelines.size(),
            instance.time_seconds);

        for (std::size_t timeline_index = 0;
             timeline_index < animation.bone_rotate_timelines.size();
             ++timeline_index) {
            const LegacyRotateTimeline& timeline = animation.bone_rotate_timelines[timeline_index];
            if (timeline.bone_index >= instance.local_poses.size()) {
                continue;
            }
            const std::optional<double> rotation = sample_legacy_rotate_timeline(
                timeline,
                instance.time_seconds,
                &instance.sampling_context.rotate_last_keyframe_indices[timeline_index]);
            if (rotation.has_value()) {
                instance.local_poses[timeline.bone_index].rotation = *rotation;
            }
        }
        for (std::size_t timeline_index = 0;
             timeline_index < animation.bone_translate_timelines.size();
             ++timeline_index) {
            const LegacyVectorTimeline& timeline = animation.bone_translate_timelines[timeline_index];
            if (timeline.bone_index >= instance.local_poses.size()) {
                continue;
            }
            const auto sample = sample_legacy_vector_timeline(
                timeline,
                instance.time_seconds,
                &instance.sampling_context.translate_last_keyframe_indices[timeline_index]);
            if (sample.has_value()) {
                instance.local_poses[timeline.bone_index].x = sample->x;
                instance.local_poses[timeline.bone_index].y = sample->y;
            }
        }
        for (std::size_t timeline_index = 0;
             timeline_index < animation.bone_scale_timelines.size();
             ++timeline_index) {
            const LegacyVectorTimeline& timeline = animation.bone_scale_timelines[timeline_index];
            if (timeline.bone_index >= instance.local_poses.size()) {
                continue;
            }
            const auto sample = sample_legacy_vector_timeline(
                timeline,
                instance.time_seconds,
                &instance.sampling_context.scale_last_keyframe_indices[timeline_index]);
            if (sample.has_value()) {
                instance.local_poses[timeline.bone_index].scale_x = sample->x;
                instance.local_poses[timeline.bone_index].scale_y = sample->y;
            }
        }
        for (std::size_t timeline_index = 0;
             timeline_index < animation.bone_shear_timelines.size();
             ++timeline_index) {
            const LegacyVectorTimeline& timeline = animation.bone_shear_timelines[timeline_index];
            if (timeline.bone_index >= instance.local_poses.size()) {
                continue;
            }
            const auto sample = sample_legacy_vector_timeline(
                timeline,
                instance.time_seconds,
                &instance.sampling_context.shear_last_keyframe_indices[timeline_index]);
            if (sample.has_value()) {
                instance.local_poses[timeline.bone_index].shear_x = sample->x;
                instance.local_poses[timeline.bone_index].shear_y = sample->y;
            }
        }

        if (checksum_out != nullptr && !instance.local_poses.empty()) {
            *checksum_out +=
                instance.local_poses.back().x * 0.001 +
                instance.local_poses.back().rotation * 0.0001;
        }
    }
}

double run_legacy_animation_sample(
    const Options& options,
    const StressBenchmarkAssets& assets,
    double* checksum_out) {
    using clock = std::chrono::steady_clock;

    std::vector<LegacyAnimationInstance> instances =
        make_legacy_animation_instances(assets, options.skeleton_count);
    double warmup_checksum = 0.0;
    for (std::size_t frame = 0; frame < kStressWarmupFrameCount; ++frame) {
        run_legacy_animation_frame(&instances, assets, &warmup_checksum);
    }

    double measured_checksum = 0.0;
    const auto start = clock::now();
    for (std::size_t frame = 0; frame < options.frame_count; ++frame) {
        run_legacy_animation_frame(&instances, assets, &measured_checksum);
    }
    if (checksum_out != nullptr) {
        *checksum_out += warmup_checksum * 1e-6 + measured_checksum;
    }

    const double total_microseconds =
        std::chrono::duration<double, std::micro>(clock::now() - start).count();
    const double work_units =
        static_cast<double>(options.frame_count) * static_cast<double>(options.skeleton_count);
    return work_units > 0.0 ? total_microseconds / work_units : 0.0;
}

void run_stress_frame(
    std::vector<StressBenchmarkInstance>* instances,
    const StressBenchmarkAssets& assets,
    StressSampleMetrics* metrics_out) {
    if (instances == nullptr) {
        return;
    }

    using clock = std::chrono::steady_clock;
    marrow::runtime::ProfilerCapture profiler(metrics_out != nullptr);
    if (metrics_out != nullptr) {
        profiler.begin_frame();
    }
    for (StressBenchmarkInstance& instance : *instances) {
        marrow::runtime::profile_phase(&profiler, marrow::runtime::ProfilerPhase::Animation, [&]() {
            instance.animation_state.update(kStressDeltaSeconds);
            instance.animation_state.apply_pose(instance.skeleton);
        });

        const std::size_t constraint_allocations_before =
            instance.skeleton.constraint_allocation_count();
        marrow::runtime::WorldTransformTimingBreakdown timing_breakdown;
        instance.skeleton.update_world_transforms(
            marrow::runtime::PhysicsMode::Pose,
            metrics_out != nullptr ? &timing_breakdown : nullptr);
        const std::size_t constraint_allocations_after =
            instance.skeleton.constraint_allocation_count();
        if (metrics_out != nullptr) {
            metrics_out->constraint_allocations += static_cast<double>(
                constraint_allocations_after - constraint_allocations_before);
            metrics_out->ik_constraint_solves +=
                static_cast<double>(timing_breakdown.evaluated_ik_constraints);
            metrics_out->ik_constraint_skips +=
                static_cast<double>(timing_breakdown.skipped_ik_constraints);
            metrics_out->transform_constraint_solves +=
                static_cast<double>(timing_breakdown.evaluated_transform_constraints);
            metrics_out->transform_constraint_skips +=
                static_cast<double>(timing_breakdown.skipped_transform_constraints);
            profiler.add_world_transform_timing(timing_breakdown);
        }

        marrow::runtime::profile_phase(&profiler, marrow::runtime::ProfilerPhase::Skinning, [&]() {
            const marrow::renderer::PreparedSceneCacheResult scene_result =
                marrow::renderer::prepare_setup_pose_scene_cached(
                    &instance.render_cache,
                    instance.skeleton,
                    *assets.atlas_data);
            if (!scene_result) {
                throw std::runtime_error(scene_result.error_message);
            }
            if (metrics_out != nullptr && scene_result.scene != nullptr) {
                metrics_out->checksum +=
                    static_cast<double>(scene_result.scene->draw_commands.size()) * 0.01;
                if (!scene_result.scene->bone_palette.empty()) {
                    metrics_out->checksum +=
                        scene_result.scene->bone_palette.front().world_x * 0.0001;
                }
            }
        });

        marrow::runtime::profile_phase(&profiler, marrow::runtime::ProfilerPhase::Render, [&]() {
            const marrow::renderer::PreparedSceneBatchSummary* batch_summary =
                marrow::renderer::summarize_prepared_scene_batches_cached(
                    &instance.render_cache);
            if (batch_summary == nullptr || !(*batch_summary)) {
                throw std::runtime_error(
                    batch_summary != nullptr && batch_summary->error_message.has_value()
                        ? *batch_summary->error_message
                        : "prepared scene cache did not produce a batch summary");
            }
            if (metrics_out != nullptr) {
                profiler.add_draw_stats(profiler_draw_stats(*batch_summary));
                metrics_out->checksum +=
                    static_cast<double>(batch_summary->vertex_count) * 0.001 +
                    static_cast<double>(batch_summary->draw_call_count);
            }
        });
        if (metrics_out != nullptr && !instance.skeleton.bone_world_transforms().empty()) {
            metrics_out->checksum +=
                instance.skeleton.bone_world_transforms().back().world_x * 0.001;
        }
    }

    if (metrics_out != nullptr) {
        profiler.end_frame();
        const marrow::runtime::ProfilerFrame frame = marrow::runtime::marrow_profiler_frame(profiler);
        metrics_out->animation_us += static_cast<double>(frame.animation_us);
        metrics_out->transform_us += static_cast<double>(frame.transform_us);
        metrics_out->constraint_us += static_cast<double>(frame.constraint_us);
        metrics_out->skinning_us += static_cast<double>(frame.skinning_us);
        metrics_out->render_us += static_cast<double>(frame.render_us);
        metrics_out->frame_ms += static_cast<double>(frame.total_us) / 1000.0;
        metrics_out->draw_calls += static_cast<double>(frame.draw_calls);
        metrics_out->streamed_vertices += static_cast<double>(frame.vertices);
        metrics_out->batch_merges += static_cast<double>(frame.batch_merges);
        metrics_out->texture_breaks +=
            static_cast<double>(frame.batch_break_reasons.texture_changes);
        metrics_out->blend_breaks +=
            static_cast<double>(frame.batch_break_reasons.blend_changes);
        metrics_out->clip_breaks +=
            static_cast<double>(frame.batch_break_reasons.clip_changes);
    }
}

StressSampleMetrics run_stress_sample(
    const Options& options,
    const StressBenchmarkAssets& assets) {
    using clock = std::chrono::steady_clock;
    StressSampleMetrics metrics;
    std::vector<StressBenchmarkInstance> instances =
        make_stress_instances(assets, options.skeleton_count);
    StressSampleMetrics warmup_metrics;

    for (std::size_t frame = 0; frame < kStressWarmupFrameCount; ++frame) {
        run_stress_frame(&instances, assets, &warmup_metrics);
    }

    const auto profiled_start = clock::now();
    for (std::size_t frame = 0; frame < options.frame_count; ++frame) {
        run_stress_frame(&instances, assets, &metrics);
    }
    metrics.profiled_wall_frame_ms =
        std::chrono::duration<double, std::milli>(clock::now() - profiled_start).count() /
        static_cast<double>(options.frame_count);

    std::vector<StressBenchmarkInstance> baseline_instances =
        make_stress_instances(assets, options.skeleton_count);
    for (std::size_t frame = 0; frame < kStressWarmupFrameCount; ++frame) {
        run_stress_frame(&baseline_instances, assets, nullptr);
    }
    const auto baseline_start = clock::now();
    for (std::size_t frame = 0; frame < options.frame_count; ++frame) {
        run_stress_frame(&baseline_instances, assets, nullptr);
    }
    metrics.baseline_frame_ms =
        std::chrono::duration<double, std::milli>(clock::now() - baseline_start).count() /
        static_cast<double>(options.frame_count);
    metrics.profiler_overhead_pct =
        metrics.baseline_frame_ms > 0.0
        ? ((metrics.profiled_wall_frame_ms / metrics.baseline_frame_ms) - 1.0) * 100.0
        : 0.0;
    metrics.legacy_animation_us =
        run_legacy_animation_sample(options, assets, &metrics.checksum);

    const double work_units =
        static_cast<double>(options.frame_count) * static_cast<double>(options.skeleton_count);
    metrics.animation_us /= work_units;
    metrics.transform_us /= work_units;
    metrics.constraint_us /= work_units;
    metrics.ik_constraint_solves /= work_units;
    metrics.ik_constraint_skips /= work_units;
    metrics.transform_constraint_solves /= work_units;
    metrics.transform_constraint_skips /= work_units;
    metrics.skinning_us /= work_units;
    metrics.render_us /= work_units;
    metrics.frame_ms /= static_cast<double>(options.frame_count);
    metrics.draw_calls /= static_cast<double>(options.frame_count);
    metrics.streamed_vertices /= static_cast<double>(options.frame_count);
    metrics.batch_merges /= static_cast<double>(options.frame_count);
    metrics.texture_breaks /= static_cast<double>(options.frame_count);
    metrics.blend_breaks /= static_cast<double>(options.frame_count);
    metrics.clip_breaks /= static_cast<double>(options.frame_count);
    return metrics;
}

double mean_of(const std::vector<double>& values) {
    if (values.empty()) {
        return 0.0;
    }

    return std::accumulate(values.begin(), values.end(), 0.0) /
        static_cast<double>(values.size());
}

double coefficient_of_variation_pct(const std::vector<double>& values) {
    if (values.size() < 2U) {
        return 0.0;
    }

    const double mean = mean_of(values);
    if (mean <= 0.0) {
        return 0.0;
    }

    double variance = 0.0;
    for (const double value : values) {
        const double delta = value - mean;
        variance += delta * delta;
    }
    variance /= static_cast<double>(values.size() - 1U);
    return std::sqrt(variance) / mean * 100.0;
}

template <typename Fn>
double mean_metric(const std::vector<StressSampleMetrics>& samples, Fn&& fn) {
    if (samples.empty()) {
        return 0.0;
    }

    double total = 0.0;
    for (const StressSampleMetrics& sample : samples) {
        total += fn(sample);
    }
    return total / static_cast<double>(samples.size());
}

StressBenchmarkResult run_stress_benchmark(const Options& options) {
    if (options.skeleton_count == 0U) {
        throw std::invalid_argument("stress benchmark requires at least one skeleton");
    }
    if (options.frame_count == 0U || options.sample_count == 0U) {
        throw std::invalid_argument("stress benchmark requires non-zero frame and sample counts");
    }

    const StressBenchmarkAssets assets = make_stress_benchmark_assets(options);
    std::vector<StressSampleMetrics> samples;
    samples.reserve(options.sample_count);
    for (std::size_t sample_index = 0; sample_index < options.sample_count; ++sample_index) {
        samples.push_back(run_stress_sample(options, assets));
    }

    std::vector<double> frame_times;
    frame_times.reserve(samples.size());
    for (const StressSampleMetrics& sample : samples) {
        frame_times.push_back(sample.frame_ms);
    }

    StressBenchmarkResult result;
    result.skeleton_count = options.skeleton_count;
    result.bone_count = options.bone_count;
    result.slot_count = assets.slot_count;
    result.clip_attachment_count = assets.clip_attachment_count;
    result.total_mesh_vertices = assets.total_mesh_vertices;
    result.ik_constraint_count = assets.ik_constraint_count;
    result.transform_constraint_count = assets.transform_constraint_count;
    result.animation_us = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.animation_us;
    });
    result.legacy_animation_us = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.legacy_animation_us;
    });
    result.transform_us = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.transform_us;
    });
    result.constraint_us = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.constraint_us;
    });
    result.ik_constraint_solves = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.ik_constraint_solves;
    });
    result.ik_constraint_skips = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.ik_constraint_skips;
    });
    result.transform_constraint_solves = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.transform_constraint_solves;
    });
    result.transform_constraint_skips = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.transform_constraint_skips;
    });
    result.constraint_allocations = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.constraint_allocations;
    });
    result.skinning_us = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.skinning_us;
    });
    result.render_us = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.render_us;
    });
    result.baseline_frame_ms = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.baseline_frame_ms;
    });
    result.profiled_wall_frame_ms = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.profiled_wall_frame_ms;
    });
    result.profiler_overhead_pct = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.profiler_overhead_pct;
    });
    result.draw_calls = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.draw_calls;
    });
    result.streamed_vertices = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.streamed_vertices;
    });
    result.batch_merges = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.batch_merges;
    });
    result.texture_breaks = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.texture_breaks;
    });
    result.blend_breaks = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.blend_breaks;
    });
    result.clip_breaks = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.clip_breaks;
    });
    result.frame_ms = mean_of(frame_times);
    result.variance_pct = coefficient_of_variation_pct(frame_times);
    result.checksum = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.checksum;
    });

    result.max_skeletons_at_60fps =
        result.frame_ms > 0.0
        ? (static_cast<double>(options.skeleton_count) * kTargetFrameBudgetMilliseconds) /
            result.frame_ms
        : 0.0;

    const double complexity_ratio =
        static_cast<double>(options.bone_count) / static_cast<double>(kMediumStressBoneCount);
    result.medium_equivalent_max_skeletons_at_60fps =
        complexity_ratio > 0.0 ? result.max_skeletons_at_60fps / complexity_ratio : 0.0;
    result.animation_speedup_pct =
        result.legacy_animation_us > 0.0
        ? (1.0 - (result.animation_us / result.legacy_animation_us)) * 100.0
        : 0.0;
    result.authored_timeline_count = assets.authored_timeline_count;
    result.optimized_timeline_count = assets.optimized_timeline_count;
    result.authored_keyframe_count = assets.authored_keyframe_count;
    result.optimized_keyframe_count = assets.optimized_keyframe_count;
    result.authored_animation_bytes = assets.authored_animation_bytes;
    result.optimized_animation_bytes = assets.optimized_animation_bytes;
    result.animation_memory_reduction_pct =
        result.authored_animation_bytes > 0U
        ? (1.0 - (static_cast<double>(result.optimized_animation_bytes) /
                  static_cast<double>(result.authored_animation_bytes))) *
            100.0
        : 0.0;

    const double raw_score =
        (result.medium_equivalent_max_skeletons_at_60fps /
         static_cast<double>(kTargetMediumSkeletonCount)) * 100.0;
    result.score = std::clamp(static_cast<int>(std::lround(raw_score)), 0, 100);
    return result;
}

int run_stress_mode(const Options& options) {
    const StressBenchmarkResult result = run_stress_benchmark(options);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "stress skeletons=" << result.skeleton_count
              << " bones=" << result.bone_count
              << " slots=" << result.slot_count
              << " clips=" << result.clip_attachment_count
              << " mesh_vertices=" << result.total_mesh_vertices
              << " ik_constraints=" << result.ik_constraint_count
              << " transform_constraints=" << result.transform_constraint_count
              << " constraint_drive="
              << constraint_drive_mode_name(options.constraint_drive_mode)
              << " frames=" << options.frame_count
              << " samples=" << options.sample_count
              << " delta=" << kStressDeltaSeconds << '\n';
    std::cout << "animation_us=" << result.animation_us
              << " legacy_animation_us=" << result.legacy_animation_us
              << " animation_speedup_pct=" << result.animation_speedup_pct
              << " transform_us=" << result.transform_us
              << " skinning_us=" << result.skinning_us
              << " constraint_us=" << result.constraint_us
              << " constraint_allocations=" << result.constraint_allocations
              << " render_us=" << result.render_us << '\n';
    std::cout << "ik_constraint_solves=" << result.ik_constraint_solves
              << " ik_constraint_skips=" << result.ik_constraint_skips
              << " transform_constraint_solves=" << result.transform_constraint_solves
              << " transform_constraint_skips=" << result.transform_constraint_skips
              << '\n';
    std::cout << "authored_timelines=" << result.authored_timeline_count
              << " optimized_timelines=" << result.optimized_timeline_count
              << " authored_keyframes=" << result.authored_keyframe_count
              << " optimized_keyframes=" << result.optimized_keyframe_count
              << " authored_animation_bytes=" << result.authored_animation_bytes
              << " optimized_animation_bytes=" << result.optimized_animation_bytes
              << " animation_memory_reduction_pct=" << result.animation_memory_reduction_pct
              << '\n';
    std::cout << "draw_calls=" << result.draw_calls
              << " streamed_vertices=" << result.streamed_vertices
              << " batch_merges=" << result.batch_merges
              << " break_texture=" << result.texture_breaks
              << " break_blend=" << result.blend_breaks
              << " break_clip=" << result.clip_breaks << '\n';
    std::cout << "frame_ms=" << result.frame_ms
              << " baseline_frame_ms=" << result.baseline_frame_ms
              << " profiled_wall_frame_ms=" << result.profiled_wall_frame_ms
              << " profiler_overhead_pct=" << result.profiler_overhead_pct
              << " max_skeletons_60fps=" << result.max_skeletons_at_60fps
              << " medium_equivalent_max_skeletons_60fps="
              << result.medium_equivalent_max_skeletons_at_60fps << '\n';
    std::cout << "score=" << result.score
              << " variance_pct=" << result.variance_pct
              << " checksum=" << result.checksum << '\n';

    const double profiler_overhead_ms = std::max(
        0.0,
        result.profiled_wall_frame_ms - result.baseline_frame_ms);
    if (result.profiler_overhead_pct > kProfilerOverheadBudgetPct &&
        profiler_overhead_ms > kProfilerOverheadBudgetMilliseconds) {
        std::cerr << "Runtime profiler overhead exceeded the "
                  << kProfilerOverheadBudgetPct
                  << "% / "
                  << kProfilerOverheadBudgetMilliseconds
                  << "ms budget.\n";
        return 1;
    }

    if (result.variance_pct >= 10.0) {
        std::cerr << "Stress benchmark variance exceeded the 10% reproducibility budget.\n";
        return 1;
    }
    if (result.constraint_allocations > 0.0) {
        std::cerr << "Constraint hot path performed heap allocations during measured frames.\n";
        return 1;
    }

    return 0;
}

int run_runtime_stress_mode(const Options& options) {
    const RuntimeStressResult stress_result = run_runtime_stress_benchmark(options);
    const double frames_per_second =
        stress_result.total_milliseconds > 0.0
        ? (static_cast<double>(kRuntimeStressFrameCount) * 1000.0) /
            stress_result.total_milliseconds
        : 0.0;
    const double full_rate_us_per_skeleton =
        (stress_result.full_rate_milliseconds * 1000.0) /
        (static_cast<double>(kRuntimeStressFrameCount * kRuntimeStressFullRateCount));
    const double throttled_us_per_skeleton =
        (stress_result.throttled_milliseconds * 1000.0) /
        (static_cast<double>(kRuntimeStressFrameCount * kRuntimeStressThrottledCount));
    const double culled_us_per_skeleton =
        (stress_result.culled_milliseconds * 1000.0) /
        (static_cast<double>(kRuntimeStressFrameCount * kRuntimeStressCulledCount));

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "runtime_stress skeleton=" << options.runtime_stress_skeleton_path.string()
              << " frames=" << kRuntimeStressFrameCount
              << " delta=" << kRuntimeStressDeltaSeconds << '\n';
    std::cout << "full_us=" << full_rate_us_per_skeleton
              << " throttled_us=" << throttled_us_per_skeleton
              << " culled_us=" << culled_us_per_skeleton
              << " fps=" << frames_per_second << '\n';
    std::cout << "checksum=" << stress_result.checksum << '\n';

    if (culled_us_per_skeleton > full_rate_us_per_skeleton * 0.10) {
        std::cerr << "Expected hidden skeleton cost to stay near-zero relative to full-rate updates.\n";
        return 1;
    }
    if (frames_per_second < 60.0) {
        std::cerr << "Runtime stress benchmark did not sustain 60 fps.\n";
        return 1;
    }

    return 0;
}

std::vector<StressBenchmarkInstance> make_animation_layer_instances(
    const AnimationLayerBenchmarkAssets& assets,
    std::size_t skeleton_count,
    bool layered) {
    std::vector<StressBenchmarkInstance> instances;
    instances.reserve(skeleton_count);
    for (std::size_t skeleton_index = 0; skeleton_index < skeleton_count; ++skeleton_index) {
        instances.emplace_back(assets.skeleton_data);
        StressBenchmarkInstance& instance = instances.back();
        instance.animation_state.set_animation(0, "walk", true, 0.0);
        if (!layered) {
            continue;
        }

        std::shared_ptr<marrow::runtime::TrackEntry> breathing =
            instance.animation_state.set_animation(1, "breathing", true, 0.0);
        breathing->blend_mode = marrow::runtime::AnimationLayerBlendMode::Additive;
        breathing->alpha = 0.5;

        std::shared_ptr<marrow::runtime::TrackEntry> aim =
            instance.animation_state.set_animation(2, "aim", true, 0.0);
        aim->blend_mode = marrow::runtime::AnimationLayerBlendMode::Override;
        aim->alpha = 1.0;
        aim->bone_filter = assets.aim_bone_filter;
    }
    return instances;
}

double run_animation_layer_frames(
    std::vector<StressBenchmarkInstance>* instances,
    std::size_t frame_count,
    double* checksum_out) {
    if (instances == nullptr) {
        return 0.0;
    }

    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    double checksum = 0.0;
    for (std::size_t frame = 0; frame < frame_count; ++frame) {
        for (StressBenchmarkInstance& instance : *instances) {
            instance.animation_state.update(kAnimationLayerDeltaSeconds);
            instance.animation_state.apply_pose(instance.skeleton);
            instance.skeleton.update_world_transforms(marrow::runtime::PhysicsMode::Pose);
            if (!instance.skeleton.bone_world_transforms().empty()) {
                checksum += instance.skeleton.bone_world_transforms().back().world_x * 0.001;
            }
        }
    }

    if (checksum_out != nullptr) {
        *checksum_out += checksum;
    }
    return elapsed_milliseconds<clock>(start);
}

AnimationLayerBenchmarkResult run_animation_layer_benchmark(const Options& options) {
    if (options.skeleton_count == 0U || options.frame_count == 0U) {
        throw std::invalid_argument(
            "animation layer benchmark requires non-zero skeleton and frame counts");
    }

    const AnimationLayerBenchmarkAssets assets = make_animation_layer_benchmark_assets(options);
    auto single_layer_instances =
        make_animation_layer_instances(assets, options.skeleton_count, false);
    auto layered_instances =
        make_animation_layer_instances(assets, options.skeleton_count, true);

    (void)run_animation_layer_frames(
        &single_layer_instances,
        kAnimationLayerWarmupFrameCount,
        nullptr);
    (void)run_animation_layer_frames(
        &layered_instances,
        kAnimationLayerWarmupFrameCount,
        nullptr);

    AnimationLayerBenchmarkResult result;
    result.single_layer_milliseconds = run_animation_layer_frames(
        &single_layer_instances,
        options.frame_count,
        &result.checksum);
    result.layered_milliseconds = run_animation_layer_frames(
        &layered_instances,
        options.frame_count,
        &result.checksum);

    const double work_units =
        static_cast<double>(options.frame_count) * static_cast<double>(options.skeleton_count);
    result.single_layer_us_per_skeleton =
        (result.single_layer_milliseconds * 1000.0) / work_units;
    result.layered_us_per_skeleton =
        (result.layered_milliseconds * 1000.0) / work_units;
    result.overhead_pct = result.single_layer_milliseconds > 0.0
        ? ((result.layered_milliseconds / result.single_layer_milliseconds) - 1.0) * 100.0
        : 0.0;
    return result;
}

int run_animation_layer_mode(const Options& options) {
    const AnimationLayerBenchmarkResult result = run_animation_layer_benchmark(options);

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "animation_layers skeletons=" << options.skeleton_count
              << " bones=" << options.bone_count
              << " frames=" << options.frame_count
              << " delta=" << kAnimationLayerDeltaSeconds << '\n';
    std::cout << "single_us=" << result.single_layer_us_per_skeleton
              << " layered_us=" << result.layered_us_per_skeleton
              << " overhead_pct=" << result.overhead_pct << '\n';
    std::cout << "checksum=" << result.checksum << '\n';

    if (result.overhead_pct > 50.0) {
        std::cerr << "Animation layering overhead exceeded the 50% budget.\n";
        return 1;
    }

    return 0;
}

int run_simd_mode(const Options& options) {
    constexpr double kScaleX = 1.37;
    constexpr double kScaleY = 0.83;
    constexpr double kMinimumSimdSpeedup = 1.5;
    constexpr std::size_t kWorldBytesPerBone = sizeof(marrow::runtime::BoneWorldTransform);

    const auto path = marrow::runtime::detail::active_bone_transform_propagation_path();
    if (path == marrow::runtime::detail::BoneTransformPropagationPath::Scalar) {
        std::cerr << "marrow_benchmark requires an SSE2 or NEON build to validate SIMD speedup.\n";
        return 1;
    }

    const std::size_t bone_count =
        options.bone_count == kDefaultStressBoneCount ? kDefaultSimdBoneCount : options.bone_count;
    if (bone_count < 2U) {
        std::cerr << "SIMD propagation mode requires at least 2 bones.\n";
        return 1;
    }

    const std::vector<marrow::runtime::BoneData> bones =
        make_simd_benchmark_bones(bone_count);
    const std::vector<marrow::runtime::BonePose> poses =
        make_simd_benchmark_poses(bones);
    std::vector<float> local_x;
    std::vector<float> local_y;
    std::vector<float> local_a;
    std::vector<float> local_b;
    std::vector<float> local_c;
    std::vector<float> local_d;
    const marrow::runtime::detail::BoneLocalTransformBuffers local_buffers{
        &local_x,
        &local_y,
        &local_a,
        &local_b,
        &local_c,
        &local_d,
    };
    marrow::runtime::detail::prepare_local_transform_buffers(poses, local_buffers);

    std::vector<float> scalar_a;
    std::vector<float> scalar_b;
    std::vector<float> scalar_c;
    std::vector<float> scalar_d;
    std::vector<float> scalar_world_x;
    std::vector<float> scalar_world_y;
    const marrow::runtime::detail::BoneWorldTransformBuffers scalar_world{
        &scalar_a,
        &scalar_b,
        &scalar_c,
        &scalar_d,
        &scalar_world_x,
        &scalar_world_y,
    };

    std::vector<float> optimized_a;
    std::vector<float> optimized_b;
    std::vector<float> optimized_c;
    std::vector<float> optimized_d;
    std::vector<float> optimized_world_x;
    std::vector<float> optimized_world_y;
    const marrow::runtime::detail::BoneWorldTransformBuffers optimized_world{
        &optimized_a,
        &optimized_b,
        &optimized_c,
        &optimized_d,
        &optimized_world_x,
        &optimized_world_y,
    };

    marrow::runtime::detail::propagate_world_transforms_scalar(
        bones,
        poses,
        kScaleX,
        kScaleY,
        local_buffers,
        scalar_world);
    marrow::runtime::detail::propagate_world_transforms_optimized(
        bones,
        poses,
        kScaleX,
        kScaleY,
        local_buffers,
        optimized_world);

    bool matches = true;
    matches &= compare_buffers(scalar_a, optimized_a, "a");
    matches &= compare_buffers(scalar_b, optimized_b, "b");
    matches &= compare_buffers(scalar_c, optimized_c, "c");
    matches &= compare_buffers(scalar_d, optimized_d, "d");
    matches &= compare_buffers(scalar_world_x, optimized_world_x, "world_x");
    matches &= compare_buffers(scalar_world_y, optimized_world_y, "world_y");
    if (!matches) {
        std::cerr << "SIMD propagation diverged from the scalar reference path.\n";
        return 1;
    }

    for (std::size_t iteration = 0; iteration < kWarmupIterations; ++iteration) {
        marrow::runtime::detail::propagate_world_transforms_scalar(
            bones,
            poses,
            kScaleX,
            kScaleY,
            local_buffers,
            scalar_world);
        marrow::runtime::detail::propagate_world_transforms_optimized(
            bones,
            poses,
            kScaleX,
            kScaleY,
            local_buffers,
            optimized_world);
    }

    const TimedResult scalar_result = time_iterations(options.iterations, [&]() {
        marrow::runtime::detail::propagate_world_transforms_scalar(
            bones,
            poses,
            kScaleX,
            kScaleY,
            local_buffers,
            scalar_world);
    });
    const TimedResult optimized_result = time_iterations(options.iterations, [&]() {
        marrow::runtime::detail::propagate_world_transforms_optimized(
            bones,
            poses,
            kScaleX,
            kScaleY,
            local_buffers,
            optimized_world);
    });

    const double checksum =
        scalar_result.checksum + optimized_result.checksum +
        scalar_world_x.back() + optimized_world_x.back() +
        scalar_a[bone_count / 2U] + optimized_a[bone_count / 2U];
    const double speedup =
        optimized_result.milliseconds > 0.0
        ? scalar_result.milliseconds / optimized_result.milliseconds
        : 0.0;

    std::cout << std::fixed << std::setprecision(2);
    std::cout << "path=" << marrow::runtime::detail::bone_transform_propagation_path_name(path)
              << " bones=" << bone_count
              << " iterations=" << options.iterations
              << " world_bytes_per_bone=" << kWorldBytesPerBone << '\n';
    std::cout << "scalar_ms=" << scalar_result.milliseconds
              << " optimized_ms=" << optimized_result.milliseconds
              << " speedup=" << speedup << "x\n";
    std::cout << "checksum=" << checksum << '\n';
    if (speedup < kMinimumSimdSpeedup) {
        std::cerr << "SIMD propagation speedup fell below the 1.5x regression threshold.\n";
        return 1;
    }
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    try {
        options = parse_options(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        print_usage(argv[0] != nullptr ? argv[0] : "marrow_benchmark");
        return 1;
    }

    try {
        switch (options.mode) {
        case BenchmarkMode::StressHarness:
            return run_stress_mode(options);
        case BenchmarkMode::RuntimeStress:
            return run_runtime_stress_mode(options);
        case BenchmarkMode::AnimationLayers:
            return run_animation_layer_mode(options);
        case BenchmarkMode::SimdPropagation:
            return run_simd_mode(options);
        }
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return 1;
    }

    return 1;
}
