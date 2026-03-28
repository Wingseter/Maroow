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
constexpr double kProfilerOverheadBudgetPct = 1.0;
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

struct Options {
    std::size_t skeleton_count{kDefaultStressSkeletonCount};
    std::size_t bone_count{kDefaultStressBoneCount};
    std::size_t frame_count{kDefaultStressFrameCount};
    std::size_t sample_count{kDefaultStressSampleCount};
    std::size_t iterations{kDefaultIterations};
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

struct StressBenchmarkAssets {
    std::shared_ptr<const marrow::runtime::SkeletonData> skeleton_data;
    std::shared_ptr<const marrow::runtime::AtlasData> atlas_data;
    std::vector<std::size_t> mesh_slot_indices;
    std::size_t slot_count{0};
    std::size_t total_mesh_vertices{0};
    std::size_t ik_constraint_count{0};
    std::size_t transform_constraint_count{0};
};

struct StressBenchmarkInstance {
    explicit StressBenchmarkInstance(std::shared_ptr<const marrow::runtime::SkeletonData> data)
        : skeleton(std::move(data)),
          animation_state(skeleton.data()) {}

    marrow::runtime::Skeleton skeleton;
    marrow::runtime::AnimationState animation_state;
};

struct StressSampleMetrics {
    double animation_us{0.0};
    double transform_us{0.0};
    double skinning_us{0.0};
    double constraint_us{0.0};
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
    std::size_t total_mesh_vertices{0};
    std::size_t ik_constraint_count{0};
    std::size_t transform_constraint_count{0};
    double animation_us{0.0};
    double transform_us{0.0};
    double skinning_us{0.0};
    double constraint_us{0.0};
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
    double variance_pct{0.0};
    int score{0};
    double checksum{0.0};
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
              << " [--skeletons N] [--bones N] [--frames N] [--samples N]\n"
              << "  " << program
              << " --simd-propagation [--bones N] [--iterations N]\n"
              << "  " << program
              << " --animation-layers [--skeletons N] [--bones N] [--frames N]\n"
              << "  " << program
              << " --runtime-stress skeleton.mskl\n";
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
    std::size_t slot_count) {
    using marrow::runtime::AnimationData;
    using marrow::runtime::BoneRotateTimeline;
    using marrow::runtime::BoneScaleTimeline;
    using marrow::runtime::BoneShearTimeline;
    using marrow::runtime::BoneTranslateTimeline;
    using marrow::runtime::ColorKeyframe;
    using marrow::runtime::Interpolation;
    using marrow::runtime::RotateKeyframe;
    using marrow::runtime::SlotColor;
    using marrow::runtime::SlotColorTimeline;
    using marrow::runtime::VectorKeyframe;

    AnimationData animation;
    animation.name = "benchmark";
    animation.targeted_bone_indices.reserve(bones.size());
    animation.bone_rotate_timelines.reserve(bones.size());
    animation.bone_translate_timelines.reserve(bones.size());

    for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
        animation.targeted_bone_indices.push_back(bone_index);
        const marrow::runtime::BoneTransform& setup = bones[bone_index].setup_pose;

        BoneRotateTimeline rotate;
        rotate.bone_index = bone_index;
        rotate.setup_rotation = setup.rotation;
        rotate.keyframes = {
            RotateKeyframe{
                0.0,
                setup.rotation - 6.0 - static_cast<double>(bone_index % 3U),
                Interpolation::linear(),
            },
            RotateKeyframe{
                0.5,
                setup.rotation + 8.0 + static_cast<double>(bone_index % 5U),
                Interpolation::linear(),
            },
            RotateKeyframe{
                1.0,
                setup.rotation - 4.0,
                Interpolation::linear(),
            },
        };
        animation.bone_rotate_timelines.push_back(std::move(rotate));

        BoneTranslateTimeline translate;
        translate.bone_index = bone_index;
        translate.keyframes = {
            VectorKeyframe{0.0, setup.x - 1.0, setup.y, Interpolation::linear()},
            VectorKeyframe{
                0.5,
                setup.x + 2.5 + static_cast<double>(bone_index % 4U) * 0.25,
                setup.y + (bone_index % 2U == 0U ? 1.25 : -1.25),
                Interpolation::linear(),
            },
            VectorKeyframe{1.0, setup.x - 0.75, setup.y, Interpolation::linear()},
        };
        animation.bone_translate_timelines.push_back(std::move(translate));

        if ((bone_index % 3U) == 0U) {
            BoneScaleTimeline scale;
            scale.bone_index = bone_index;
            scale.keyframes = {
                VectorKeyframe{0.0, setup.scale_x, setup.scale_y, Interpolation::linear()},
                VectorKeyframe{0.5, setup.scale_x * 1.05, setup.scale_y * 0.97, Interpolation::linear()},
                VectorKeyframe{1.0, setup.scale_x, setup.scale_y, Interpolation::linear()},
            };
            animation.bone_scale_timelines.push_back(std::move(scale));
        }

        if ((bone_index % 5U) == 0U) {
            BoneShearTimeline shear;
            shear.bone_index = bone_index;
            shear.keyframes = {
                VectorKeyframe{0.0, setup.shear_x, setup.shear_y, Interpolation::linear()},
                VectorKeyframe{0.5, setup.shear_x + 2.0, setup.shear_y - 1.0, Interpolation::linear()},
                VectorKeyframe{1.0, setup.shear_x, setup.shear_y, Interpolation::linear()},
            };
            animation.bone_shear_timelines.push_back(std::move(shear));
        }
    }

    for (std::size_t slot_index = 0; slot_index < slot_count; ++slot_index) {
        SlotColorTimeline color;
        color.slot_index = slot_index;
        color.keyframes = {
            ColorKeyframe{0.0, SlotColor{1.0, 1.0, 1.0, 1.0}, Interpolation::linear()},
            ColorKeyframe{
                0.5,
                SlotColor{
                    1.0,
                    0.9 - static_cast<double>(slot_index % 3U) * 0.05,
                    0.85 - static_cast<double>(slot_index % 4U) * 0.04,
                    1.0,
                },
                Interpolation::linear(),
            },
            ColorKeyframe{1.0, SlotColor{1.0, 1.0, 1.0, 1.0}, Interpolation::linear()},
        };
        animation.slot_color_timelines.push_back(std::move(color));
    }

    return animation;
}

StressBenchmarkAssets make_stress_benchmark_assets(const Options& options) {
    if (options.bone_count < 4U) {
        throw std::invalid_argument("stress benchmark requires at least 4 bones");
    }

    std::vector<marrow::runtime::BoneData> bones = make_stress_bones(options.bone_count);
    const std::size_t slot_count = std::max<std::size_t>(1U, options.bone_count / 8U);
    const std::size_t total_mesh_vertices = std::max<std::size_t>(32U, options.bone_count * 4U);
    const std::size_t vertices_per_slot = round_up_even(std::max<std::size_t>(
        4U,
        total_mesh_vertices / slot_count));
    const std::size_t bones_per_slot = std::max<std::size_t>(1U, options.bone_count / slot_count);
    const std::size_t atlas_columns = std::max<std::size_t>(
        1U,
        static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(slot_count)))));
    const std::size_t atlas_rows = (slot_count + atlas_columns - 1U) / atlas_columns;

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
    regions.reserve(slot_count);
    mesh_slot_indices.reserve(slot_count);

    for (std::size_t slot_index = 0; slot_index < slot_count; ++slot_index) {
        const std::size_t desired_bone =
            std::min<std::size_t>(1U + slot_index * bones_per_slot, options.bone_count - 1U);
        const std::size_t bone_index = std::min<std::size_t>(
            desired_bone,
            options.bone_count > 4U ? options.bone_count - 4U : options.bone_count - 1U);
        const std::string attachment_name = "mesh_" + std::to_string(slot_index);

        marrow::runtime::SlotData slot;
        slot.name = "slot_" + std::to_string(slot_index);
        slot.bone_index = bone_index;
        slot.setup_attachment = attachment_name;
        slot.color = marrow::runtime::SlotColor{
            1.0,
            0.95 - static_cast<double>(slot_index % 3U) * 0.04,
            0.92 - static_cast<double>(slot_index % 4U) * 0.03,
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
        region.x = static_cast<double>((slot_index % atlas_columns) * 128U);
        region.y = static_cast<double>((slot_index / atlas_columns) * 128U);
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

    marrow::runtime::AnimationData animation = make_stress_animation(bones, slot_count);
    marrow::runtime::SkeletonInfo info;
    info.name = "benchmark_medium";
    info.width = static_cast<double>(slot_count) * 96.0;
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
    assets.mesh_slot_indices = std::move(mesh_slot_indices);
    assets.slot_count = slot_count;
    assets.total_mesh_vertices = slot_count * vertices_per_slot;
    assets.ik_constraint_count = assets.skeleton_data->ik_constraints().size();
    assets.transform_constraint_count = assets.skeleton_data->transform_constraints().size();
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

        marrow::runtime::WorldTransformTimingBreakdown timing_breakdown;
        instance.skeleton.update_world_transforms(
            marrow::runtime::PhysicsMode::Pose,
            metrics_out != nullptr ? &timing_breakdown : nullptr);
        if (metrics_out != nullptr) {
            profiler.add_world_transform_timing(timing_breakdown);
        }

        marrow::runtime::profile_phase(&profiler, marrow::runtime::ProfilerPhase::Skinning, [&]() {
            for (const std::size_t slot_index : assets.mesh_slot_indices) {
                const std::optional<marrow::runtime::MeshAttachmentPose> mesh_pose =
                    instance.skeleton.evaluate_current_mesh_attachment(slot_index);
                if (!mesh_pose.has_value()) {
                    throw std::runtime_error(
                        "stress benchmark failed to evaluate mesh slot " +
                        std::to_string(slot_index));
                }
                if (metrics_out != nullptr && !mesh_pose->vertices.empty()) {
                    metrics_out->checksum += mesh_pose->vertices.front().x * 0.0001;
                }
            }
        });

        marrow::runtime::profile_phase(&profiler, marrow::runtime::ProfilerPhase::Render, [&]() {
            const marrow::renderer::PreparedSceneResult scene_result =
                marrow::renderer::prepare_setup_pose_scene(
                    instance.skeleton,
                    *assets.atlas_data);
            if (!scene_result) {
                throw std::runtime_error(scene_result.error_message);
            }

            const marrow::renderer::PreparedSceneBatchSummary batch_summary =
                marrow::renderer::summarize_prepared_scene_batches(*scene_result.scene);
            if (!batch_summary) {
                throw std::runtime_error(*batch_summary.error_message);
            }
            if (metrics_out != nullptr) {
                profiler.add_draw_stats(profiler_draw_stats(batch_summary));
                metrics_out->checksum +=
                    static_cast<double>(batch_summary.vertex_count) * 0.001 +
                    static_cast<double>(batch_summary.draw_call_count);
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

    const double work_units =
        static_cast<double>(options.frame_count) * static_cast<double>(options.skeleton_count);
    metrics.animation_us /= work_units;
    metrics.transform_us /= work_units;
    metrics.constraint_us /= work_units;
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
    result.total_mesh_vertices = assets.total_mesh_vertices;
    result.ik_constraint_count = assets.ik_constraint_count;
    result.transform_constraint_count = assets.transform_constraint_count;
    result.animation_us = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.animation_us;
    });
    result.transform_us = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.transform_us;
    });
    result.constraint_us = mean_metric(samples, [](const StressSampleMetrics& sample) {
        return sample.constraint_us;
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
              << " mesh_vertices=" << result.total_mesh_vertices
              << " ik_constraints=" << result.ik_constraint_count
              << " transform_constraints=" << result.transform_constraint_count
              << " frames=" << options.frame_count
              << " samples=" << options.sample_count
              << " delta=" << kStressDeltaSeconds << '\n';
    std::cout << "animation_us=" << result.animation_us
              << " transform_us=" << result.transform_us
              << " skinning_us=" << result.skinning_us
              << " constraint_us=" << result.constraint_us
              << " render_us=" << result.render_us << '\n';
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

    if (result.profiler_overhead_pct > kProfilerOverheadBudgetPct) {
        std::cerr << "Runtime profiler overhead exceeded the 1% budget.\n";
        return 1;
    }

    if (result.variance_pct >= 10.0) {
        std::cerr << "Stress benchmark variance exceeded the 10% reproducibility budget.\n";
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
        nullptr,
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
        nullptr,
    };

    marrow::runtime::detail::propagate_world_transforms_scalar(
        bones,
        poses,
        1.0,
        1.0,
        local_buffers,
        scalar_world);
    marrow::runtime::detail::propagate_world_transforms_optimized(
        bones,
        poses,
        1.0,
        1.0,
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
            1.0,
            1.0,
            local_buffers,
            scalar_world);
        marrow::runtime::detail::propagate_world_transforms_optimized(
            bones,
            poses,
            1.0,
            1.0,
            local_buffers,
            optimized_world);
    }

    const TimedResult scalar_result = time_iterations(options.iterations, [&]() {
        marrow::runtime::detail::propagate_world_transforms_scalar(
            bones,
            poses,
            1.0,
            1.0,
            local_buffers,
            scalar_world);
    });
    const TimedResult optimized_result = time_iterations(options.iterations, [&]() {
        marrow::runtime::detail::propagate_world_transforms_optimized(
            bones,
            poses,
            1.0,
            1.0,
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
              << " iterations=" << options.iterations << '\n';
    std::cout << "scalar_ms=" << scalar_result.milliseconds
              << " optimized_ms=" << optimized_result.milliseconds
              << " speedup=" << speedup << "x\n";
    std::cout << "checksum=" << checksum << '\n';
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
