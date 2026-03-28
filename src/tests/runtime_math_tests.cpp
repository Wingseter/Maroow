#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "module_internal.hpp"
#include "marrow/allocator.hpp"
#include "marrow/runtime/animation_compare.hpp"
#include "marrow/runtime/animation.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/profiler.hpp"
#include "marrow/runtime/skeleton.hpp"
#include "skeleton_internal.hpp"

namespace {

using marrow::runtime::AttachmentData;
using marrow::runtime::AttachmentKind;
using marrow::runtime::AttachmentVertex;
using marrow::runtime::AnimationData;
using marrow::runtime::AnimationLayerBlendMode;
using marrow::runtime::AnimationMixDefinition;
using marrow::runtime::AnimationState;
using marrow::runtime::AnimationStateSnapshot;
using marrow::runtime::AnimationStateTrackEntrySnapshot;
using marrow::runtime::BoneData;
using marrow::runtime::BoneInherit;
using marrow::runtime::BonePose;
using marrow::runtime::BoneRotateTimeline;
using marrow::runtime::BoneTransform;
using marrow::runtime::BoneTranslateTimeline;
using marrow::runtime::BoneWorldTransform;
using marrow::runtime::BoundingBoxAttachmentData;
using marrow::runtime::IkConstraintData;
using marrow::runtime::Interpolation;
using marrow::runtime::PhysicsConstraintData;
using marrow::runtime::PhysicsMode;
using marrow::runtime::RotateKeyframe;
using marrow::runtime::Skeleton;
using marrow::runtime::SkeletonBounds;
using marrow::runtime::SkeletonData;
using marrow::runtime::SkeletonInfo;
using marrow::runtime::SkinData;
using marrow::runtime::SkinSlotData;
using marrow::runtime::SlotData;
namespace renderer_internal = marrow::renderer::internal;

constexpr double kPi = 3.14159265358979323846;
constexpr double kTolerance = 1e-6;

struct TestContext {
    explicit TestContext(std::string test_name)
        : name(std::move(test_name)) {}

    void expect(bool condition, std::string_view message) {
        if (condition) {
            return;
        }

        ++failures;
        std::cerr << name << ": " << message << '\n';
    }

    void expect_near(
        double actual,
        double expected,
        std::string_view message,
        double tolerance = kTolerance) {
        if (std::abs(actual - expected) <= tolerance) {
            return;
        }

        ++failures;
        std::cerr << name << ": " << message << " expected " << expected
                  << " but got " << actual << ".\n";
    }

    int failures{0};
    std::string name;
};

template <typename TestFn>
int run_test(std::string name, TestFn&& test_fn) {
    TestContext context(std::move(name));
    test_fn(context);
    if (context.failures == 0) {
        std::cout << "[PASS] " << context.name << '\n';
    } else {
        std::cout << "[FAIL] " << context.name << " (" << context.failures << ")\n";
    }
    return context.failures;
}

double degrees_to_radians(double degrees) {
    return degrees * kPi / 180.0;
}

double radians_to_degrees(double radians) {
    return radians * 180.0 / kPi;
}

double normalize_rotation_degrees(double angle) {
    return angle - (std::ceil(angle / 360.0 - 0.5) * 360.0);
}

double world_rotation_degrees(const BoneWorldTransform& transform) {
    return radians_to_degrees(std::atan2(transform.c, transform.a));
}

BoneData make_bone(
    std::string name,
    std::optional<std::size_t> parent_index,
    const BoneTransform& setup_pose = {},
    BoneInherit inherit = BoneInherit::Normal) {
    BoneData bone;
    bone.name = std::move(name);
    bone.parent_index = parent_index;
    bone.setup_pose = setup_pose;
    bone.inherit = inherit;
    return bone;
}

std::shared_ptr<const SkeletonData> make_skeleton_data(
    std::vector<BoneData> bones,
    std::vector<IkConstraintData> ik_constraints = {},
    std::vector<PhysicsConstraintData> physics_constraints = {},
    std::vector<SlotData> slots = {},
    std::vector<SkinData> skins = {},
    std::vector<AnimationData> animations = {},
    double default_mix_duration = 0.0,
    std::vector<AnimationMixDefinition> mix_definitions = {}) {
    SkeletonInfo info;
    info.name = "runtime_math_unit_tests";
    return marrow::allocate_shared<SkeletonData>(
        std::move(info),
        std::move(bones),
        std::move(ik_constraints),
        std::vector<marrow::runtime::PathConstraintData>{},
        std::vector<marrow::runtime::TransformConstraintData>{},
        std::move(physics_constraints),
        std::move(slots),
        std::vector<marrow::runtime::EventDefinition>{},
        std::move(animations),
        std::move(skins),
        default_mix_duration,
        std::move(mix_definitions));
}

AttachmentData make_bounding_box_attachment(
    std::string name,
    std::vector<AttachmentVertex> polygon) {
    AttachmentData attachment;
    attachment.name = std::move(name);
    attachment.kind = AttachmentKind::BoundingBox;
    attachment.bounding_box = BoundingBoxAttachmentData{std::move(polygon)};
    return attachment;
}

BoneWorldTransform reference_local_transform(const BoneTransform& transform) {
    const double rotation_x = degrees_to_radians(transform.rotation + transform.shear_x);
    const double rotation_y = degrees_to_radians(transform.rotation + 90.0 + transform.shear_y);

    BoneWorldTransform world_transform;
    world_transform.a = std::cos(rotation_x) * transform.scale_x;
    world_transform.b = std::cos(rotation_y) * transform.scale_y;
    world_transform.c = std::sin(rotation_x) * transform.scale_x;
    world_transform.d = std::sin(rotation_y) * transform.scale_y;
    world_transform.world_x = transform.x;
    world_transform.world_y = transform.y;
    return world_transform;
}

void expect_world_transform(
    TestContext& context,
    const BoneWorldTransform& actual,
    const BoneWorldTransform& expected,
    std::string_view label,
    double tolerance = kTolerance) {
    context.expect_near(actual.a, expected.a, std::string(label) + " a", tolerance);
    context.expect_near(actual.b, expected.b, std::string(label) + " b", tolerance);
    context.expect_near(actual.c, expected.c, std::string(label) + " c", tolerance);
    context.expect_near(actual.d, expected.d, std::string(label) + " d", tolerance);
    context.expect_near(
        actual.world_x,
        expected.world_x,
        std::string(label) + " world_x",
        tolerance);
    context.expect_near(
        actual.world_y,
        expected.world_y,
        std::string(label) + " world_y",
        tolerance);
}

std::shared_ptr<const SkeletonData> make_two_bone_ik_data(
    double target_x,
    double target_y,
    bool bend_positive,
    bool stretch = false) {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone("upper", 0, BoneTransform{}));
    bones.push_back(make_bone("lower", 1, BoneTransform{5.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone("hand", 2, BoneTransform{5.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone("target", 0, BoneTransform{target_x, target_y, 0.0, 1.0, 1.0, 0.0, 0.0}));

    IkConstraintData constraint;
    constraint.name = "arm";
    constraint.bone_indices = {1, 2};
    constraint.target_bone_index = 4;
    constraint.mix = 1.0;
    constraint.bend_positive = bend_positive;
    constraint.stretch = stretch;
    return make_skeleton_data(std::move(bones), {constraint});
}

std::shared_ptr<const SkeletonData> make_degenerate_ik_data() {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone("upper", 0, BoneTransform{}));
    bones.push_back(make_bone("lower", 1, BoneTransform{0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone("hand", 2, BoneTransform{5.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone("target", 0, BoneTransform{0.0, 5.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    IkConstraintData constraint;
    constraint.name = "degenerate_arm";
    constraint.bone_indices = {1, 2};
    constraint.target_bone_index = 4;
    constraint.mix = 1.0;
    constraint.bend_positive = true;
    return make_skeleton_data(std::move(bones), {constraint});
}

std::shared_ptr<const SkeletonData> make_one_bone_wrap_ik_data() {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone(
        "turret",
        0,
        BoneTransform{0.0, 0.0, 350.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone("muzzle", 1, BoneTransform{10.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "target",
        0,
        BoneTransform{
            10.0 * std::cos(degrees_to_radians(10.0)),
            10.0 * std::sin(degrees_to_radians(10.0)),
            0.0,
            1.0,
            1.0,
            0.0,
            0.0}));

    IkConstraintData constraint;
    constraint.name = "turret_track";
    constraint.bone_indices = {1};
    constraint.target_bone_index = 3;
    constraint.mix = 0.5;
    return make_skeleton_data(std::move(bones), {constraint});
}

std::shared_ptr<const SkeletonData> make_physics_test_data() {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone("swing", 0, BoneTransform{10.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone("tip", 1, BoneTransform{10.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    PhysicsConstraintData physics;
    physics.name = "secondary";
    physics.bone_indices = {1};
    physics.step = 1.0 / 60.0;
    physics.x = 1.0;
    physics.y = 1.0;
    physics.rotate = 1.0;
    physics.scale_x = 0.0;
    physics.shear_x = 0.0;
    physics.limit = 1000.0;
    physics.inertia = 0.85;
    physics.damping = 6.0;
    physics.strength = 25.0;
    physics.mass_inverse = 1.0;
    physics.mix = 1.0;

    return make_skeleton_data(std::move(bones), {}, {physics});
}

std::shared_ptr<const SkeletonData> make_bounds_test_data() {
    std::vector<BoneData> bones;
    bones.push_back(make_bone(
        "root",
        std::nullopt,
        BoneTransform{5.0, 7.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    SlotData slot;
    slot.name = "hurtbox";
    slot.bone_index = 0;
    slot.setup_attachment = "hitbox";

    SkinData default_skin;
    default_skin.name = "default";
    default_skin.slot_attachments.push_back(SkinSlotData{
        0,
        make_bounding_box_attachment(
            "hitbox",
            {
                {-1.0, -1.0},
                {1.0, -1.0},
                {1.0, 1.0},
                {-1.0, 1.0},
            })});

    return make_skeleton_data(std::move(bones), {}, {}, {slot}, {default_skin});
}

AnimationData make_rotate_animation(
    std::string name,
    std::size_t bone_index,
    std::vector<std::pair<double, double>> keys) {
    AnimationData animation;
    animation.name = std::move(name);
    animation.targeted_bone_indices.push_back(bone_index);

    BoneRotateTimeline timeline;
    timeline.bone_index = bone_index;
    timeline.setup_rotation = 0.0;
    timeline.keyframes.reserve(keys.size());
    for (const auto& [time, angle] : keys) {
        timeline.keyframes.push_back(
            RotateKeyframe{time, angle, Interpolation::linear()});
    }

    animation.bone_rotate_timelines.push_back(std::move(timeline));
    return animation;
}

std::shared_ptr<const SkeletonData> make_animation_layer_test_data() {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone(
        "spine",
        0,
        BoneTransform{0.0, 10.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "arm",
        1,
        BoneTransform{5.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    AnimationData walk;
    walk.name = "walk";
    walk.targeted_bone_indices = {1, 2};
    walk.bone_rotate_timelines.push_back(BoneRotateTimeline{
        1,
        0.0,
        {
            RotateKeyframe{0.0, 0.0, Interpolation::linear()},
            RotateKeyframe{0.5, 10.0, Interpolation::linear()},
            RotateKeyframe{1.0, 0.0, Interpolation::linear()},
        }});
    walk.bone_rotate_timelines.push_back(BoneRotateTimeline{
        2,
        0.0,
        {
            RotateKeyframe{0.0, 0.0, Interpolation::linear()},
            RotateKeyframe{0.5, 15.0, Interpolation::linear()},
            RotateKeyframe{1.0, 0.0, Interpolation::linear()},
        }});

    AnimationData breathing;
    breathing.name = "breathing";
    breathing.targeted_bone_indices = {1};
    breathing.bone_rotate_timelines.push_back(BoneRotateTimeline{
        1,
        0.0,
        {
            RotateKeyframe{0.0, 0.0, Interpolation::linear()},
            RotateKeyframe{0.5, 4.0, Interpolation::linear()},
            RotateKeyframe{1.0, 0.0, Interpolation::linear()},
        }});
    BoneTranslateTimeline breathing_translate;
    breathing_translate.bone_index = 1;
    breathing_translate.keyframes = {
        marrow::runtime::VectorKeyframe{0.0, 0.0, 10.0, Interpolation::linear()},
        marrow::runtime::VectorKeyframe{0.5, 0.0, 16.0, Interpolation::linear()},
        marrow::runtime::VectorKeyframe{1.0, 0.0, 10.0, Interpolation::linear()},
    };
    breathing.bone_translate_timelines.push_back(std::move(breathing_translate));

    AnimationData aim;
    aim.name = "aim";
    aim.targeted_bone_indices = {1, 2};
    aim.bone_rotate_timelines.push_back(BoneRotateTimeline{
        1,
        0.0,
        {
            RotateKeyframe{0.0, 0.0, Interpolation::linear()},
            RotateKeyframe{0.5, 30.0, Interpolation::linear()},
            RotateKeyframe{1.0, 0.0, Interpolation::linear()},
        }});
    aim.bone_rotate_timelines.push_back(BoneRotateTimeline{
        2,
        0.0,
        {
            RotateKeyframe{0.0, 0.0, Interpolation::linear()},
            RotateKeyframe{0.5, 60.0, Interpolation::linear()},
            RotateKeyframe{1.0, 0.0, Interpolation::linear()},
        }});
    BoneTranslateTimeline aim_translate;
    aim_translate.bone_index = 2;
    aim_translate.keyframes = {
        marrow::runtime::VectorKeyframe{0.0, 5.0, 0.0, Interpolation::linear()},
        marrow::runtime::VectorKeyframe{0.5, 8.0, 2.0, Interpolation::linear()},
        marrow::runtime::VectorKeyframe{1.0, 5.0, 0.0, Interpolation::linear()},
    };
    aim.bone_translate_timelines.push_back(std::move(aim_translate));

    return make_skeleton_data(
        std::move(bones),
        {},
        {},
        {},
        {},
        {std::move(walk), std::move(breathing), std::move(aim)});
}

std::shared_ptr<const SkeletonData> make_animation_state_snapshot_test_data(double attack_peak) {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("arm", std::nullopt));

    std::vector<AnimationData> animations;
    animations.push_back(make_rotate_animation(
        "walk",
        0,
        {{0.0, 0.0}, {1.0, 20.0}}));
    animations.push_back(make_rotate_animation(
        "attack",
        0,
        {{0.0, 0.0}, {0.2, attack_peak}, {0.4, attack_peak}}));
    animations.push_back(make_rotate_animation(
        "idle",
        0,
        {{0.0, -5.0}, {1.0, 5.0}}));
    animations.push_back(make_rotate_animation(
        "overlay",
        0,
        {{0.0, 5.0}, {1.0, -5.0}}));

    std::vector<AnimationMixDefinition> mix_definitions;
    mix_definitions.push_back(AnimationMixDefinition{"walk", "attack", 0.2, false});
    mix_definitions.push_back(AnimationMixDefinition{"attack", "idle", 0.1, false});

    return make_skeleton_data(
        std::move(bones),
        {},
        {},
        {},
        {},
        std::move(animations),
        0.05,
        std::move(mix_definitions));
}

void configure_animation_state_snapshot_graph(
    AnimationState* state,
    Skeleton* skeleton) {
    auto walk = state->set_animation(0, "walk", true, 0.0);
    walk->alpha = 0.85;
    state->update(0.5);
    if (skeleton != nullptr) {
        state->apply(*skeleton);
    }

    auto attack = state->set_animation(0, "attack", false, 0.2);
    attack->alpha = 0.75;

    auto idle = state->add_animation(0, "idle", true, 0.1, 0.1);
    idle->reverse = true;
    idle->alpha = 0.4;

    auto overlay = state->set_animation(1, "overlay", true, 0.0);
    overlay->alpha = 0.25;
    overlay->reverse = true;
    overlay->blend_mode = AnimationLayerBlendMode::Additive;
    overlay->bone_filter = {0};

    state->update(0.1);
}

void expect_track_entry_snapshot_equal(
    TestContext& context,
    const AnimationStateTrackEntrySnapshot& actual,
    const AnimationStateTrackEntrySnapshot& expected,
    std::string_view label) {
    context.expect(
        actual.track_index == expected.track_index,
        std::string(label) + " track_index");
    context.expect(
        actual.animation_name == expected.animation_name,
        std::string(label) + " animation_name");
    context.expect(actual.loop == expected.loop, std::string(label) + " loop");
    context.expect(actual.is_empty == expected.is_empty, std::string(label) + " is_empty");
    context.expect(actual.reverse == expected.reverse, std::string(label) + " reverse");
    context.expect_near(actual.alpha, expected.alpha, std::string(label) + " alpha");
    context.expect(
        actual.blend_mode == expected.blend_mode,
        std::string(label) + " blend_mode");
    context.expect(
        actual.bone_filter == expected.bone_filter,
        std::string(label) + " bone_filter");
    context.expect_near(
        actual.mix_duration,
        expected.mix_duration,
        std::string(label) + " mix_duration");
    context.expect_near(actual.mix_time, expected.mix_time, std::string(label) + " mix_time");
    context.expect_near(actual.track_time, expected.track_time, std::string(label) + " track_time");
    context.expect(actual.track_last == expected.track_last, std::string(label) + " track_last");
    context.expect(
        actual.next_track_last == expected.next_track_last,
        std::string(label) + " next_track_last");
    context.expect(actual.track_end == expected.track_end, std::string(label) + " track_end");
    context.expect_near(actual.delay, expected.delay, std::string(label) + " delay");
    context.expect_near(
        actual.interrupt_alpha,
        expected.interrupt_alpha,
        std::string(label) + " interrupt_alpha");
    context.expect_near(
        actual.total_alpha,
        expected.total_alpha,
        std::string(label) + " total_alpha");
    context.expect(
        actual.timelines_rotation == expected.timelines_rotation,
        std::string(label) + " timelines_rotation");
    context.expect(
        actual.snapshot_frozen == expected.snapshot_frozen,
        std::string(label) + " snapshot_frozen");
    context.expect(
        actual.has_started == expected.has_started,
        std::string(label) + " has_started");
    context.expect(
        actual.start_notified_to_entry == expected.start_notified_to_entry,
        std::string(label) + " start_notified");
    context.expect_near(
        actual.last_track_time,
        expected.last_track_time,
        std::string(label) + " last_track_time");
    context.expect(
        actual.next_entry_index == expected.next_entry_index,
        std::string(label) + " next_entry_index");
    context.expect(
        actual.mixing_from_entry_index == expected.mixing_from_entry_index,
        std::string(label) + " mixing_from_entry_index");
    context.expect(
        actual.snapshot_bone_poses.size() == expected.snapshot_bone_poses.size(),
        std::string(label) + " snapshot_bone_pose_count");
    for (std::size_t index = 0;
         index < actual.snapshot_bone_poses.size() &&
         index < expected.snapshot_bone_poses.size();
         ++index) {
        const BonePose& actual_pose = actual.snapshot_bone_poses[index];
        const BonePose& expected_pose = expected.snapshot_bone_poses[index];
        const std::string pose_label =
            std::string(label) + " snapshot_bone[" + std::to_string(index) + "]";
        context.expect_near(
            actual_pose.local_pose.rotation,
            expected_pose.local_pose.rotation,
            pose_label + " rotation");
        context.expect_near(
            actual_pose.local_pose.x,
            expected_pose.local_pose.x,
            pose_label + " x");
        context.expect_near(
            actual_pose.local_pose.y,
            expected_pose.local_pose.y,
            pose_label + " y");
        context.expect(
            actual_pose.inherit == expected_pose.inherit,
            pose_label + " inherit");
    }
    context.expect(
        actual.snapshot_slot_states.size() == expected.snapshot_slot_states.size(),
        std::string(label) + " snapshot_slot_state_count");
    for (std::size_t index = 0;
         index < actual.snapshot_slot_states.size() &&
         index < expected.snapshot_slot_states.size();
         ++index) {
        const auto& actual_slot = actual.snapshot_slot_states[index];
        const auto& expected_slot = expected.snapshot_slot_states[index];
        const std::string slot_label =
            std::string(label) + " snapshot_slot[" + std::to_string(index) + "]";
        context.expect(
            actual_slot.attachment_name == expected_slot.attachment_name,
            slot_label + " attachment_name");
        context.expect(
            actual_slot.attachment_skin_index == expected_slot.attachment_skin_index,
            slot_label + " attachment_skin_index");
        context.expect_near(actual_slot.color.r, expected_slot.color.r, slot_label + " color.r");
        context.expect_near(actual_slot.color.g, expected_slot.color.g, slot_label + " color.g");
        context.expect_near(actual_slot.color.b, expected_slot.color.b, slot_label + " color.b");
        context.expect_near(actual_slot.color.a, expected_slot.color.a, slot_label + " color.a");
        context.expect(
            actual_slot.dark_color.has_value() == expected_slot.dark_color.has_value(),
            slot_label + " dark_color_present");
        if (actual_slot.dark_color.has_value() && expected_slot.dark_color.has_value()) {
            context.expect_near(
                actual_slot.dark_color->r,
                expected_slot.dark_color->r,
                slot_label + " dark_color.r");
            context.expect_near(
                actual_slot.dark_color->g,
                expected_slot.dark_color->g,
                slot_label + " dark_color.g");
            context.expect_near(
                actual_slot.dark_color->b,
                expected_slot.dark_color->b,
                slot_label + " dark_color.b");
            context.expect_near(
                actual_slot.dark_color->a,
                expected_slot.dark_color->a,
                slot_label + " dark_color.a");
        }
    }
    context.expect(
        actual.snapshot_mesh_deforms.size() == expected.snapshot_mesh_deforms.size(),
        std::string(label) + " snapshot_mesh_deform_count");
    for (std::size_t index = 0;
         index < actual.snapshot_mesh_deforms.size() &&
         index < expected.snapshot_mesh_deforms.size();
         ++index) {
        const auto& actual_deform = actual.snapshot_mesh_deforms[index];
        const auto& expected_deform = expected.snapshot_mesh_deforms[index];
        const std::string deform_label =
            std::string(label) + " snapshot_mesh[" + std::to_string(index) + "]";
        context.expect(
            actual_deform.attachment_name == expected_deform.attachment_name,
            deform_label + " attachment_name");
        context.expect(
            actual_deform.vertex_offsets == expected_deform.vertex_offsets,
            deform_label + " vertex_offsets");
    }
    context.expect(
        actual.snapshot_draw_order == expected.snapshot_draw_order,
        std::string(label) + " snapshot_draw_order");
}

void expect_animation_state_snapshot_equal(
    TestContext& context,
    const AnimationStateSnapshot& actual,
    const AnimationStateSnapshot& expected,
    std::string_view label) {
    context.expect(
        actual.track_roots == expected.track_roots,
        std::string(label) + " track_roots");
    context.expect(
        actual.entries.size() == expected.entries.size(),
        std::string(label) + " entry_count");
    for (std::size_t index = 0;
         index < actual.entries.size() && index < expected.entries.size();
         ++index) {
        expect_track_entry_snapshot_equal(
            context,
            actual.entries[index],
            expected.entries[index],
            std::string(label) + " entry[" + std::to_string(index) + "]");
    }
}

class CountingAllocator final : public marrow::Allocator {
public:
    void* allocate(std::size_t size, std::size_t alignment) override {
        if (size == 0U) {
            size = 1U;
        }

        alignment = std::max(alignment, alignof(Header));
        if ((alignment & (alignment - 1U)) != 0U) {
            std::size_t rounded = 1U;
            while (rounded < alignment) {
                rounded <<= 1U;
            }
            alignment = rounded;
        }

        const std::size_t total_size = size + (alignment - 1U) + sizeof(Header);
        void* base = std::malloc(total_size);
        if (base == nullptr) {
            return nullptr;
        }

        void* aligned = static_cast<char*>(base) + sizeof(Header);
        std::size_t space = total_size - sizeof(Header);
        aligned = std::align(alignment, size, aligned, space);
        if (aligned == nullptr) {
            std::free(base);
            return nullptr;
        }

        auto* header = reinterpret_cast<Header*>(static_cast<char*>(aligned) - sizeof(Header));
        header->base = base;
        ++allocation_count;
        ++live_allocations;
        return aligned;
    }

    void deallocate(void* ptr, std::size_t) noexcept override {
        if (ptr == nullptr) {
            return;
        }

        const auto* header = reinterpret_cast<const Header*>(
            static_cast<const char*>(ptr) - sizeof(Header));
        std::free(header->base);
        ++deallocation_count;
        if (live_allocations > 0U) {
            --live_allocations;
        }
    }

    std::size_t allocation_count{0U};
    std::size_t deallocation_count{0U};
    std::size_t live_allocations{0U};

private:
    struct Header {
        void* base{nullptr};
    };
};

void test_interpolation_edges(TestContext& context) {
    const Interpolation linear = Interpolation::linear();
    context.expect_near(linear.transform(0.0), 0.0, "linear alpha=0");
    context.expect_near(linear.transform(1.0), 1.0, "linear alpha=1");
    context.expect_near(
        marrow::runtime::interpolate_value(3.0, 9.0, linear, 0.0),
        3.0,
        "linear interpolation start");
    context.expect_near(
        marrow::runtime::interpolate_value(3.0, 9.0, linear, 1.0),
        9.0,
        "linear interpolation end");

    const Interpolation bezier = Interpolation::cubic_bezier(0.0, 1.0, 1.0, 0.0);
    context.expect_near(bezier.transform(0.0), 0.0, "bezier alpha=0");
    context.expect_near(bezier.transform(1.0), 1.0, "bezier alpha=1");
    context.expect(bezier.transform(0.25) > 0.25, "bezier 0.25 should ease above linear");
    context.expect(bezier.transform(0.75) < 0.75, "bezier 0.75 should ease below linear");
    context.expect_near(bezier.transform(-1.0), 0.0, "bezier clamps below zero");
    context.expect_near(bezier.transform(2.0), 1.0, "bezier clamps above one");

    BoneRotateTimeline stepped_timeline;
    stepped_timeline.bone_index = 0;
    stepped_timeline.setup_rotation = 15.0;
    stepped_timeline.keyframes = {
        RotateKeyframe{0.0, 0.0, Interpolation::stepped()},
        RotateKeyframe{1.0, 90.0, Interpolation::linear()},
    };

    const std::optional<double> halfway = marrow::runtime::sample_rotate_timeline(
        stepped_timeline,
        0.5);
    const std::optional<double> at_end = marrow::runtime::sample_rotate_timeline(
        stepped_timeline,
        1.0);
    context.expect(halfway.has_value(), "stepped timeline should sample at 0.5");
    context.expect(at_end.has_value(), "stepped timeline should sample at 1.0");
    if (halfway.has_value()) {
        context.expect_near(*halfway, 15.0, "stepped timeline holds previous value");
    }
    if (at_end.has_value()) {
        context.expect_near(*at_end, 105.0, "stepped timeline switches at the keyframe");
    }
}

void test_matrix_composition(TestContext& context) {
    const BoneWorldTransform identity =
        marrow::runtime::detail::root_world_transform(BoneTransform{}, 1.0, 1.0);
    expect_world_transform(context, identity, BoneWorldTransform{}, "identity");

    BoneTransform rotation_transform;
    rotation_transform.rotation = 90.0;
    const BoneWorldTransform rotation =
        marrow::runtime::detail::root_world_transform(rotation_transform, 1.0, 1.0);
    context.expect_near(rotation.a, 0.0, "rotation a");
    context.expect_near(rotation.b, -1.0, "rotation b");
    context.expect_near(rotation.c, 1.0, "rotation c");
    context.expect_near(rotation.d, 0.0, "rotation d");

    BoneTransform scale_transform;
    scale_transform.scale_x = 2.0;
    scale_transform.scale_y = 3.0;
    const BoneWorldTransform scale =
        marrow::runtime::detail::root_world_transform(scale_transform, 1.0, 1.0);
    context.expect_near(scale.a, 2.0, "scale a");
    context.expect_near(scale.d, 3.0, "scale d");

    BoneTransform shear_transform;
    shear_transform.shear_x = 30.0;
    shear_transform.shear_y = -15.0;
    shear_transform.scale_x = 2.0;
    shear_transform.scale_y = 1.5;
    const BoneWorldTransform shear =
        marrow::runtime::detail::root_world_transform(shear_transform, 1.0, 1.0);
    context.expect_near(
        shear.a,
        std::cos(degrees_to_radians(30.0)) * 2.0,
        "shear a");
    context.expect_near(
        shear.b,
        std::cos(degrees_to_radians(75.0)) * 1.5,
        "shear b");
    context.expect_near(
        shear.c,
        std::sin(degrees_to_radians(30.0)) * 2.0,
        "shear c");
    context.expect_near(
        shear.d,
        std::sin(degrees_to_radians(75.0)) * 1.5,
        "shear d");

    BoneTransform parent_transform;
    parent_transform.x = 3.0;
    parent_transform.y = 4.0;
    parent_transform.rotation = 90.0;
    parent_transform.scale_x = 2.0;
    parent_transform.scale_y = 1.0;
    const BoneWorldTransform parent =
        marrow::runtime::detail::root_world_transform(parent_transform, 1.0, 1.0);

    BonePose child_pose;
    child_pose.local_pose = BoneTransform{5.0, 2.0, 30.0, 1.5, 0.5, 0.0, 0.0};
    child_pose.inherit = BoneInherit::Normal;
    const BoneWorldTransform child = marrow::runtime::detail::compose_world_transform(
        parent,
        child_pose,
        1.0,
        1.0);

    const BoneWorldTransform child_local = reference_local_transform(child_pose.local_pose);
    BoneWorldTransform expected_child;
    expected_child.world_x =
        parent.a * child_pose.local_pose.x + parent.b * child_pose.local_pose.y + parent.world_x;
    expected_child.world_y =
        parent.c * child_pose.local_pose.x + parent.d * child_pose.local_pose.y + parent.world_y;
    expected_child.a = parent.a * child_local.a + parent.b * child_local.c;
    expected_child.b = parent.a * child_local.b + parent.b * child_local.d;
    expected_child.c = parent.c * child_local.a + parent.d * child_local.c;
    expected_child.d = parent.c * child_local.b + parent.d * child_local.d;
    expect_world_transform(context, child, expected_child, "parent-child");
}

void test_topological_bone_reorder(TestContext& context) {
    SlotData hand_slot;
    hand_slot.name = "hand_slot";
    hand_slot.bone_index = 0;

    AnimationData animation;
    animation.name = "wave";
    animation.targeted_bone_indices = {0, 2};
    animation.bone_rotate_timelines.push_back(BoneRotateTimeline{
        0,
        0.0,
        {RotateKeyframe{0.0, 0.0, Interpolation::linear()}}});

    std::vector<BoneData> bones;
    bones.push_back(make_bone("hand", 2, BoneTransform{5.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone("arm", 1, BoneTransform{2.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    const auto skeleton_data = make_skeleton_data(
        std::move(bones),
        {},
        {},
        {hand_slot},
        {},
        {animation});

    context.expect(skeleton_data->bones()[0].name == "root", "root should sort before descendants");
    context.expect(skeleton_data->bones()[1].name == "arm", "arm should sort after root");
    context.expect(skeleton_data->bones()[2].name == "hand", "hand should sort after arm");
    context.expect(
        skeleton_data->bones()[1].parent_index == std::optional<std::size_t>{0},
        "arm parent should remap to root");
    context.expect(
        skeleton_data->bones()[2].parent_index == std::optional<std::size_t>{1},
        "hand parent should remap to arm");
    context.expect(
        skeleton_data->slots()[0].bone_index == 2,
        "slot bone index should remap alongside bones");

    const AnimationData* reordered_animation = skeleton_data->find_animation("wave");
    context.expect(reordered_animation != nullptr, "animation should survive topological reorder");
    if (reordered_animation == nullptr) {
        return;
    }

    context.expect(
        reordered_animation->bone_rotate_timelines[0].bone_index == 2,
        "animation bone indices should remap to the reordered skeleton");

    Skeleton skeleton(skeleton_data);
    context.expect_near(
        skeleton.bone_world_transforms()[2].world_x,
        7.0,
        "topologically reordered hand world x");
    context.expect_near(
        skeleton.bone_world_transforms()[2].world_y,
        0.0,
        "topologically reordered hand world y");
}

void test_simd_world_transform_propagation(TestContext& context) {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    for (std::size_t parent_index = 0; bones.size() < 17; ++parent_index) {
        for (std::size_t sibling = 0; sibling < 4 && bones.size() < 17; ++sibling) {
            BoneTransform setup_pose;
            setup_pose.x = 1.0 + static_cast<double>(sibling) * 0.5;
            setup_pose.y = -0.5 + static_cast<double>(bones.size() % 5) * 0.3;
            setup_pose.rotation = static_cast<double>((bones.size() * 17) % 360);
            setup_pose.scale_x = 0.85 + static_cast<double>(bones.size() % 7) * 0.05;
            setup_pose.scale_y = 0.9 + static_cast<double>(bones.size() % 6) * 0.04;
            setup_pose.shear_x = -3.0 + static_cast<double>(bones.size() % 5) * 1.5;
            setup_pose.shear_y = -2.0 + static_cast<double>(bones.size() % 7) * 0.75;

            const BoneInherit inherit =
                bones.size() >= 9 && bones.size() < 13
                ? BoneInherit::OnlyTranslation
                : BoneInherit::Normal;
            bones.push_back(make_bone(
                "bone_" + std::to_string(bones.size()),
                parent_index,
                setup_pose,
                inherit));
        }
    }

    std::vector<BonePose> poses;
    poses.reserve(bones.size());
    for (const BoneData& bone : bones) {
        poses.push_back(BonePose{bone.setup_pose, bone.inherit});
    }

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

    for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
        context.expect_near(optimized_a[bone_index], scalar_a[bone_index], "simd a", 1e-5);
        context.expect_near(optimized_b[bone_index], scalar_b[bone_index], "simd b", 1e-5);
        context.expect_near(optimized_c[bone_index], scalar_c[bone_index], "simd c", 1e-5);
        context.expect_near(optimized_d[bone_index], scalar_d[bone_index], "simd d", 1e-5);
        context.expect_near(
            optimized_world_x[bone_index],
            scalar_world_x[bone_index],
            "simd world x",
            1e-5);
        context.expect_near(
            optimized_world_y[bone_index],
            scalar_world_y[bone_index],
            "simd world y",
            1e-5);
    }
}

void test_ik_cases(TestContext& context) {
    Skeleton reachable(make_two_bone_ik_data(6.0, 8.0, true));
    const BoneWorldTransform& reachable_hand = reachable.bone_world_transforms()[3];
    context.expect_near(reachable_hand.world_x, 6.0, "reachable ik hand x");
    context.expect_near(reachable_hand.world_y, 8.0, "reachable ik hand y");

    Skeleton unreachable(make_two_bone_ik_data(20.0, 0.0, true));
    const BoneWorldTransform& unreachable_hand = unreachable.bone_world_transforms()[3];
    context.expect_near(unreachable_hand.world_x, 10.0, "unreachable ik hand x");
    context.expect_near(unreachable_hand.world_y, 0.0, "unreachable ik hand y");

    Skeleton degenerate(make_degenerate_ik_data());
    for (std::size_t bone_index = 0; bone_index < degenerate.bone_world_transforms().size(); ++bone_index) {
        const BoneWorldTransform& transform = degenerate.bone_world_transforms()[bone_index];
        context.expect(std::isfinite(transform.a), "degenerate ik a should remain finite");
        context.expect(std::isfinite(transform.b), "degenerate ik b should remain finite");
        context.expect(std::isfinite(transform.c), "degenerate ik c should remain finite");
        context.expect(std::isfinite(transform.d), "degenerate ik d should remain finite");
        context.expect(std::isfinite(transform.world_x), "degenerate ik x should remain finite");
        context.expect(std::isfinite(transform.world_y), "degenerate ik y should remain finite");
    }
    context.expect_near(
        degenerate.bone_world_transforms()[3].world_x,
        0.0,
        "degenerate ik hand x");
    context.expect_near(
        degenerate.bone_world_transforms()[3].world_y,
        5.0,
        "degenerate ik hand y");

    Skeleton wrap(make_one_bone_wrap_ik_data());
    const double wrap_rotation = normalize_rotation_degrees(
        world_rotation_degrees(wrap.bone_world_transforms()[1]));
    context.expect_near(wrap_rotation, 0.0, "wrap-around one-bone ik uses shortest arc");
    context.expect_near(
        wrap.bone_world_transforms()[2].world_x,
        10.0,
        "wrap-around one-bone muzzle x",
        1e-5);
    context.expect_near(
        wrap.bone_world_transforms()[2].world_y,
        0.0,
        "wrap-around one-bone muzzle y",
        1e-5);
}

void test_physics_stepping(TestContext& context) {
    const std::shared_ptr<const SkeletonData> data = make_physics_test_data();

    Skeleton authored(data);
    authored.bone_poses()[0].local_pose.rotation = 90.0;
    authored.update_world_transforms(PhysicsMode::None);
    const BoneWorldTransform authored_tip = authored.bone_world_transforms()[2];

    Skeleton simulated(data);
    simulated.update_physics(1.0 / 60.0);
    simulated.bone_poses()[0].local_pose.rotation = 90.0;
    simulated.update_physics(1.0 / 60.0);

    const BoneWorldTransform lagged_tip = simulated.bone_world_transforms()[2];
    const double lagged_distance = std::hypot(
        lagged_tip.world_x - authored_tip.world_x,
        lagged_tip.world_y - authored_tip.world_y);
    context.expect(lagged_distance > 0.5, "physics step should lag behind the authored pose");

    for (int frame = 0; frame < 180; ++frame) {
        simulated.update_physics(1.0 / 60.0);
    }

    const BoneWorldTransform settled_tip = simulated.bone_world_transforms()[2];
    const double settled_distance = std::hypot(
        settled_tip.world_x - authored_tip.world_x,
        settled_tip.world_y - authored_tip.world_y);
    context.expect(
        settled_distance < lagged_distance,
        "physics stepping should converge toward the authored pose");

    simulated.reset_physics();
    expect_world_transform(
        context,
        simulated.bone_world_transforms()[2],
        authored_tip,
        "physics reset tip",
        1e-5);
}

void test_skeleton_bounds_queries(TestContext& context) {
    Skeleton skeleton(make_bounds_test_data());
    SkeletonBounds bounds;
    bounds.update(skeleton);

    context.expect(bounds.has_aabb(), "bounds should compute an AABB");
    context.expect_near(bounds.min_x(), 4.0, "bounds min x");
    context.expect_near(bounds.min_y(), 6.0, "bounds min y");
    context.expect_near(bounds.max_x(), 6.0, "bounds max x");
    context.expect_near(bounds.max_y(), 8.0, "bounds max y");
    context.expect(bounds.bounding_boxes().size() == 1, "expected one bounding box");

    const marrow::runtime::BoundingBoxAttachmentPose* hit = nullptr;
    context.expect(bounds.contains_point(5.0, 7.0, &hit), "point inside bounds should hit");
    context.expect(hit != nullptr, "contains_point should report the hit bounding box");
    context.expect(
        hit != nullptr && hit->attachment_name == "hitbox",
        "hit bounding box should report the attachment name");
    context.expect(bounds.get_bounding_box() != nullptr, "last hit bounding box should be recorded");
    context.expect(bounds.contains_point(4.0, 7.0), "point on the edge should count as inside");
    context.expect(!bounds.contains_point(6.5, 7.0), "point outside bounds should miss");
    context.expect(bounds.get_bounding_box() == nullptr, "missed containment should clear the last hit");
    context.expect(
        bounds.intersects_segment(3.0, 7.0, 7.0, 7.0),
        "crossing segment should intersect");
    context.expect(
        bounds.get_bounding_box() != nullptr,
        "segment hit should update the last hit bounding box");
    context.expect(
        !bounds.intersects_segment(3.0, 9.5, 7.0, 9.5),
        "disjoint segment should not intersect");
    context.expect(bounds.get_bounding_box() == nullptr, "segment miss should clear the last hit");

    const std::vector<AttachmentVertex>* polygon = bounds.get_polygon("hitbox");
    context.expect(polygon != nullptr, "polygon lookup by attachment name should succeed");
    context.expect(
        polygon != nullptr && polygon->size() == 4,
        "polygon lookup should preserve all vertices");

    SkeletonBounds no_aabb_bounds;
    no_aabb_bounds.update(skeleton, false);
    context.expect(!no_aabb_bounds.has_aabb(), "compute_aabb=false should skip AABB generation");
    context.expect(
        no_aabb_bounds.contains_point(5.0, 7.0),
        "query-only bounds should still answer containment");
}

void test_custom_allocator_lifecycle(TestContext& context) {
    CountingAllocator allocator;
    if (!marrow::set_allocator(&allocator)) {
        context.expect(false, "counting allocator should install before Marrow allocations");
        return;
    }

    {
        const std::shared_ptr<const SkeletonData> data = make_bounds_test_data();
        auto skeleton = marrow::allocate_unique<Skeleton>(data);
        auto anim_state = marrow::allocate_unique<marrow::runtime::AnimationState>(data);
        auto bounds = marrow::allocate_unique<SkeletonBounds>();

        skeleton->update_world_transforms();
        bounds->update(*skeleton, true);
        anim_state->clear_tracks();

        context.expect(bounds->has_aabb(), "counting allocator lifecycle should still update bounds");
    }

    context.expect(
        allocator.allocation_count > 0U,
        "counting allocator should observe Marrow allocations");
    context.expect(
        allocator.live_allocations == 0U,
        "counting allocator should report zero live Marrow allocations after destruction");
    context.expect(
        allocator.deallocation_count == allocator.allocation_count,
        "counting allocator should deallocate every observed Marrow allocation");
    context.expect(
        marrow::set_allocator(nullptr),
        "default allocator should restore after Marrow allocations are released");
}

void test_animation_state_snapshot_restore(TestContext& context) {
    const std::shared_ptr<const SkeletonData> source_data =
        make_animation_state_snapshot_test_data(60.0);
    Skeleton source_skeleton(source_data);
    AnimationState source_state(source_data);
    configure_animation_state_snapshot_graph(&source_state, &source_skeleton);
    source_state.apply(source_skeleton);

    const AnimationStateSnapshot captured = source_state.capture_state();
    context.expect(captured.track_roots.size() == 2U, "snapshot should preserve both tracks");
    context.expect(captured.track_roots[0].has_value(), "snapshot track 0 should have a root");
    context.expect(captured.track_roots[1].has_value(), "snapshot track 1 should have a root");
    context.expect(captured.entries.size() == 4U, "snapshot should flatten current, queue, mix, and overlay entries");

    const std::shared_ptr<const SkeletonData> reloaded_data =
        make_animation_state_snapshot_test_data(90.0);

    Skeleton restored_skeleton(reloaded_data);
    AnimationState restored_state(reloaded_data);
    restored_state.restore_state(captured);
    restored_state.apply(restored_skeleton);

    Skeleton expected_skeleton(reloaded_data);
    AnimationState expected_state(reloaded_data);
    configure_animation_state_snapshot_graph(&expected_state, &expected_skeleton);
    expected_state.apply(expected_skeleton);

    expect_animation_state_snapshot_equal(
        context,
        restored_state.capture_state(),
        expected_state.capture_state(),
        "restored snapshot");

    const auto arm_index = reloaded_data->find_bone_index("arm");
    context.expect(arm_index.has_value(), "reloaded data should keep the arm bone");
    if (!arm_index.has_value()) {
        return;
    }

    context.expect_near(
        restored_skeleton.bone_poses()[*arm_index].local_pose.rotation,
        expected_skeleton.bone_poses()[*arm_index].local_pose.rotation,
        "restored pose should match a fresh rebuild against reloaded data");
    context.expect(
        std::abs(
            restored_skeleton.bone_poses()[*arm_index].local_pose.rotation -
            source_skeleton.bone_poses()[*arm_index].local_pose.rotation) > 1e-3,
        "restored pose should pick up the updated animation data after reload");

    restored_state.update(1.0 / 60.0);
    restored_state.apply(restored_skeleton);
    expected_state.update(1.0 / 60.0);
    expected_state.apply(expected_skeleton);

    expect_animation_state_snapshot_equal(
        context,
        restored_state.capture_state(),
        expected_state.capture_state(),
        "restored snapshot after advance");
    context.expect_near(
        restored_state.get_current(0)->track_time,
        expected_state.get_current(0)->track_time,
        "restored playback should continue from the captured track time");
}

void test_animation_layers(TestContext& context) {
    const std::shared_ptr<const SkeletonData> data = make_animation_layer_test_data();
    const auto spine_index = data->find_bone_index("spine");
    const auto arm_index = data->find_bone_index("arm");
    context.expect(spine_index.has_value(), "layer test should keep the spine bone");
    context.expect(arm_index.has_value(), "layer test should keep the arm bone");
    if (!spine_index.has_value() || !arm_index.has_value()) {
        return;
    }

    AnimationState state(data);
    Skeleton skeleton(data);

    auto walk = state.set_animation(0, "walk", true, 0.0);
    auto breathing = state.set_animation(1, "breathing", true, 0.0);
    breathing->blend_mode = AnimationLayerBlendMode::Additive;
    breathing->alpha = 0.5;

    auto aim = state.set_animation(2, "aim", true, 0.0);
    aim->blend_mode = AnimationLayerBlendMode::Override;
    aim->alpha = 1.0;
    aim->bone_filter = {*arm_index};

    state.update(0.5);
    state.apply(skeleton);

    context.expect(
        walk->blend_mode == AnimationLayerBlendMode::Override,
        "tracks should default to override blending");
    context.expect(
        breathing->blend_mode == AnimationLayerBlendMode::Additive,
        "breathing layer should use additive blending");
    context.expect(
        aim->bone_filter == std::vector<std::size_t>{*arm_index},
        "aim layer should preserve its bone filter");
    context.expect_near(
        skeleton.bone_poses()[*spine_index].local_pose.rotation,
        12.0,
        "spine rotation should combine walk and breathing without the aim override");
    context.expect_near(
        skeleton.bone_poses()[*spine_index].local_pose.y,
        13.0,
        "spine translation should add the breathing delta on top of walk");
    context.expect_near(
        skeleton.bone_poses()[*arm_index].local_pose.rotation,
        60.0,
        "aim override should replace the masked arm rotation");
    context.expect_near(
        skeleton.bone_poses()[*arm_index].local_pose.x,
        8.0,
        "aim override should replace the masked arm translation x");
    context.expect_near(
        skeleton.bone_poses()[*arm_index].local_pose.y,
        2.0,
        "aim override should replace the masked arm translation y");

    const AnimationStateSnapshot snapshot = state.capture_state();
    context.expect(snapshot.track_roots.size() == 3U, "layer snapshot should preserve all tracks");
    context.expect(snapshot.entries.size() == 3U, "layer snapshot should capture each active layer");
    context.expect(
        snapshot.entries[1].blend_mode == AnimationLayerBlendMode::Additive,
        "layer snapshot should preserve additive blend mode");
    context.expect(
        snapshot.entries[2].bone_filter == std::vector<std::size_t>{*arm_index},
        "layer snapshot should preserve the aim bone filter");
}

std::vector<marrow::renderer::RenderPoint> make_box(
    double min_x,
    double min_y,
    double max_x,
    double max_y) {
    return {
        {min_x, min_y},
        {max_x, min_y},
        {max_x, max_y},
        {min_x, max_y},
    };
}

void expect_visible_pixel_count(
    TestContext& context,
    const renderer_internal::SoftwareStencilBuffer& buffer,
    const std::vector<marrow::renderer::RenderPoint>& polygon,
    std::optional<std::uint8_t> required_reference,
    std::size_t expected_pixels,
    std::string_view label) {
    const std::size_t actual_pixels =
        renderer_internal::count_software_stencil_visible_pixels(
            buffer,
            polygon,
            required_reference);
    if (actual_pixels == expected_pixels) {
        return;
    }

    ++context.failures;
    std::cerr << context.name << ": " << label << " expected " << expected_pixels
              << " visible pixels but got " << actual_pixels << ".\n";
}

void test_concave_stencil_clipping(TestContext& context) {
    renderer_internal::SoftwareStencilBuffer buffer;
    if (const std::optional<std::string> error =
            renderer_internal::initialize_software_stencil_buffer(
                20,
                20,
                0.0,
                0.0,
                1.0,
                &buffer)) {
        context.expect(false, *error);
        return;
    }

    const std::optional<renderer_internal::SoftwareStencilClipState> clip_state =
        renderer_internal::stencil_clip_state_for_depth(1);
    context.expect(clip_state.has_value(), "depth-1 stencil state should be available");
    if (!clip_state.has_value()) {
        return;
    }

    const std::vector<marrow::renderer::RenderPoint> concave_clip{
        {2.0, 2.0},
        {14.0, 2.0},
        {14.0, 6.0},
        {6.0, 6.0},
        {6.0, 14.0},
        {2.0, 14.0},
    };
    if (const std::optional<std::string> error =
            renderer_internal::apply_software_stencil_clip_push(
                concave_clip,
                *clip_state,
                &buffer)) {
        context.expect(false, *error);
        return;
    }

    const std::vector<marrow::renderer::RenderPoint> fullscreen_attachment =
        make_box(0.0, 0.0, 20.0, 20.0);
    const std::vector<marrow::renderer::RenderPoint> notch_attachment =
        make_box(8.0, 8.0, 12.0, 12.0);

    expect_visible_pixel_count(
        context,
        buffer,
        fullscreen_attachment,
        clip_state->reference_value,
        80,
        "concave clip area");
    expect_visible_pixel_count(
        context,
        buffer,
        notch_attachment,
        clip_state->reference_value,
        0,
        "concave clip interior notch");
}

void test_nested_stencil_reference_restoration(TestContext& context) {
    renderer_internal::SoftwareStencilBuffer buffer;
    if (const std::optional<std::string> error =
            renderer_internal::initialize_software_stencil_buffer(
                24,
                24,
                0.0,
                0.0,
                1.0,
                &buffer)) {
        context.expect(false, *error);
        return;
    }

    const std::optional<renderer_internal::SoftwareStencilClipState> outer_clip =
        renderer_internal::stencil_clip_state_for_depth(1);
    const std::optional<renderer_internal::SoftwareStencilClipState> inner_clip =
        renderer_internal::stencil_clip_state_for_depth(2);
    context.expect(outer_clip.has_value(), "outer clip state should be available");
    context.expect(inner_clip.has_value(), "inner clip state should be available");
    if (!outer_clip.has_value() || !inner_clip.has_value()) {
        return;
    }

    context.expect(
        outer_clip->reference_value == 1 &&
            outer_clip->parent_reference_value == 0 &&
            outer_clip->invert_mask == 1,
        "outer clip should use stencil reference 1");
    context.expect(
        inner_clip->reference_value == 2 &&
            inner_clip->parent_reference_value == 1 &&
            inner_clip->invert_mask == 3,
        "nested clip should increment the stencil reference to 2");

    const std::vector<marrow::renderer::RenderPoint> outer_polygon =
        make_box(2.0, 2.0, 18.0, 18.0);
    const std::vector<marrow::renderer::RenderPoint> inner_polygon =
        make_box(8.0, 8.0, 18.0, 18.0);
    const std::vector<marrow::renderer::RenderPoint> fullscreen_attachment =
        make_box(0.0, 0.0, 24.0, 24.0);

    if (const std::optional<std::string> error =
            renderer_internal::apply_software_stencil_clip_push(
                outer_polygon,
                *outer_clip,
                &buffer)) {
        context.expect(false, *error);
        return;
    }
    expect_visible_pixel_count(
        context,
        buffer,
        fullscreen_attachment,
        outer_clip->reference_value,
        256,
        "outer clip coverage");

    if (const std::optional<std::string> error =
            renderer_internal::apply_software_stencil_clip_push(
                inner_polygon,
                *inner_clip,
                &buffer)) {
        context.expect(false, *error);
        return;
    }
    expect_visible_pixel_count(
        context,
        buffer,
        fullscreen_attachment,
        inner_clip->reference_value,
        100,
        "nested clip coverage");
    expect_visible_pixel_count(
        context,
        buffer,
        fullscreen_attachment,
        outer_clip->reference_value,
        156,
        "outer clip after nested push");

    if (const std::optional<std::string> error =
            renderer_internal::apply_software_stencil_clip_pop(
                inner_polygon,
                *inner_clip,
                &buffer)) {
        context.expect(false, *error);
        return;
    }
    expect_visible_pixel_count(
        context,
        buffer,
        fullscreen_attachment,
        inner_clip->reference_value,
        0,
        "nested clip cleared after pop");
    expect_visible_pixel_count(
        context,
        buffer,
        fullscreen_attachment,
        outer_clip->reference_value,
        256,
        "outer clip restored after nested pop");

    if (const std::optional<std::string> error =
            renderer_internal::apply_software_stencil_clip_pop(
                outer_polygon,
                *outer_clip,
                &buffer)) {
        context.expect(false, *error);
        return;
    }
    expect_visible_pixel_count(
        context,
        buffer,
        fullscreen_attachment,
        outer_clip->reference_value,
        0,
        "outer clip cleared after end slot");
}

void test_binary_key_quantization_and_reduction(TestContext& context) {
    const auto document_result = marrow::runtime::json::parse_document(
        R"json({
  "marrow": "1.0",
  "version": 1,
  "skeleton": {
    "name": "reduction_test",
    "width": 128,
    "height": 128
  },
  "bones": [
    { "name": "root" }
  ],
  "slots": [
    { "name": "body", "bone": "root", "attachment": "body" }
  ],
  "skins": {
    "default": {
      "body": {
        "attachment": "body",
        "type": "region",
        "region": "body"
      }
    }
  },
  "animations": {
    "reduce_test": {
      "bones": {
        "root": {
          "rotate": [
            { "time": 0.0, "angle": 0.0, "curve": "linear" },
            { "time": 0.125, "angle": 10.0, "curve": "linear" },
            { "time": 0.25, "angle": 20.0, "curve": "linear" },
            { "time": 0.375, "angle": 30.0, "curve": "linear" },
            { "time": 0.5, "angle": 40.0, "curve": "linear" },
            { "time": 0.625, "angle": 50.0, "curve": "linear" },
            { "time": 0.75, "angle": 60.0, "curve": "linear" },
            { "time": 0.875, "angle": 70.0, "curve": "linear" },
            { "time": 1.0, "angle": 80.0, "curve": "linear" }
          ],
          "translate": [
            { "time": 0.0, "x": 0.0, "y": 0.0, "curve": "linear" },
            { "time": 0.125, "x": 8.0, "y": 4.0, "curve": "linear" },
            { "time": 0.25, "x": 16.0, "y": 8.0, "curve": "linear" },
            { "time": 0.375, "x": 24.0, "y": 12.0, "curve": "linear" },
            { "time": 0.5, "x": 32.0, "y": 16.0, "curve": "linear" },
            { "time": 0.625, "x": 40.0, "y": 20.0, "curve": "linear" },
            { "time": 0.75, "x": 48.0, "y": 24.0, "curve": "linear" },
            { "time": 0.875, "x": 56.0, "y": 28.0, "curve": "linear" },
            { "time": 1.0, "x": 64.0, "y": 32.0, "curve": "linear" }
          ]
        }
      }
    }
  }
})json");
    context.expect(
        static_cast<bool>(document_result),
        document_result.error.has_value()
            ? document_result.error->format()
            : "synthetic reduction document should parse");
    if (!document_result) {
        return;
    }

    const auto original_result =
        marrow::runtime::load_skeleton_data(*document_result.document);
    context.expect(
        static_cast<bool>(original_result),
        original_result.error.has_value()
            ? original_result.error->format()
            : "synthetic reduction runtime should load");
    if (!original_result) {
        return;
    }

    const std::filesystem::path binary_path =
        std::filesystem::temp_directory_path() / "marrow_mar066_reduction_test.mbin";
    if (const auto error = marrow::runtime::write_skeleton_binary_document(
            *document_result.document,
            binary_path)) {
        context.expect(false, error->format());
        return;
    }

    marrow::runtime::SkeletonBinaryInspection inspection;
    if (const auto error = marrow::runtime::inspect_skeleton_binary(binary_path, &inspection)) {
        context.expect(false, error->format());
        std::filesystem::remove(binary_path);
        return;
    }
    context.expect(
        inspection.has_optimized_animation_section,
        "binary inspection should report an optimized animation payload");
    context.expect(
        inspection.keyframes_sorted_by_time_and_bone,
        "binary inspection should report sorted packed keyframes");

    const auto binary_result = marrow::runtime::load_skeleton_data(binary_path);
    context.expect(
        static_cast<bool>(binary_result),
        binary_result.error.has_value()
            ? binary_result.error->format()
            : "quantized binary should load through the path API");
    if (!binary_result) {
        std::filesystem::remove(binary_path);
        return;
    }

    const auto comparison = marrow::runtime::compare_animation_roundtrip(
        *original_result.skeleton_data,
        *binary_result.skeleton_data);
    context.expect(static_cast<bool>(comparison), "roundtrip animation comparison should succeed");
    if (comparison) {
        context.expect(
            comparison.metrics.roundtrip_rotation_keyframes <
                comparison.metrics.original_rotation_keyframes,
            "rotate key reduction should remove redundant keys");
        context.expect(
            comparison.metrics.roundtrip_translation_keyframes <
                comparison.metrics.original_translation_keyframes,
            "translate key reduction should remove redundant keys");
        context.expect(
            comparison.metrics.max_rotation_error_degrees < 0.1,
            "rotation quantization should stay below 0.1 degrees");
        context.expect(
            comparison.metrics.max_translation_error_pixels < 0.5,
            "translation quantization should stay below 0.5px");
    } else {
        context.expect(false, *comparison.error);
    }

    std::error_code remove_error;
    std::filesystem::remove(binary_path, remove_error);
}

void test_runtime_profiler_frame(TestContext& context) {
    marrow::runtime::ProfilerCapture profiler(true);
    profiler.begin_frame();
    profiler.add_phase_microseconds(marrow::runtime::ProfilerPhase::Animation, 10U);
    profiler.add_phase_microseconds(marrow::runtime::ProfilerPhase::Skinning, 20U);
    profiler.add_phase_microseconds(marrow::runtime::ProfilerPhase::Render, 30U);

    marrow::runtime::WorldTransformTimingBreakdown world_timing;
    world_timing.transform_seconds = 40.0 / 1.0e6;
    world_timing.constraint_seconds = 50.0 / 1.0e6;
    profiler.add_world_transform_timing(world_timing);

    marrow::runtime::ProfilerDrawStats draw_stats;
    draw_stats.skeleton_count = 2U;
    draw_stats.draw_calls = 4U;
    draw_stats.vertices = 128U;
    draw_stats.batch_merges = 3U;
    draw_stats.break_reasons.texture_changes = 1U;
    draw_stats.break_reasons.blend_changes = 2U;
    draw_stats.break_reasons.clip_changes = 3U;
    profiler.add_draw_stats(draw_stats);

    const marrow::runtime::ProfilerFrame frame = marrow::runtime::marrow_profiler_frame(profiler);
    context.expect(frame.animation_us == 10U, "profiler animation timing should round-trip");
    context.expect(frame.transform_us == 40U, "profiler transform timing should round-trip");
    context.expect(frame.constraint_us == 50U, "profiler constraint timing should round-trip");
    context.expect(frame.skinning_us == 20U, "profiler skinning timing should round-trip");
    context.expect(frame.render_us == 30U, "profiler render timing should round-trip");
    context.expect(frame.total_us == 150U, "profiler total timing should sum per-phase timings");
    context.expect(frame.skeleton_count == 2U, "profiler frame should expose skeleton count");
    context.expect(frame.draw_calls == 4U, "profiler frame should expose draw calls");
    context.expect(frame.vertices == 128U, "profiler frame should expose streamed vertices");
    context.expect(frame.batch_merges == 3U, "profiler frame should expose merged draw calls");
    context.expect(
        frame.batch_break_reasons.texture_changes == 1U &&
            frame.batch_break_reasons.blend_changes == 2U &&
            frame.batch_break_reasons.clip_changes == 3U,
        "profiler frame should expose batch break reasons");

    const std::vector<std::string> hud_lines = marrow::runtime::profiler_hud_lines(frame);
    context.expect(hud_lines.size() == 3U, "profiler HUD should emit three lines");
    if (hud_lines.size() == 3U) {
        context.expect(
            hud_lines[0].find("SKELS 2") != std::string::npos &&
                hud_lines[0].find("DRAWS 4") != std::string::npos &&
                hud_lines[0].find("VERTS 128") != std::string::npos,
            "profiler HUD first line should include draw counters");
        context.expect(
            hud_lines[1].find("FRAME 150US") != std::string::npos &&
                hud_lines[1].find("ANIM 10US") != std::string::npos &&
                hud_lines[1].find("XFORM 40US") != std::string::npos &&
                hud_lines[1].find("CONSTR 50US") != std::string::npos,
            "profiler HUD second line should include phase timings");
        context.expect(
            hud_lines[2].find("SKIN 20US") != std::string::npos &&
                hud_lines[2].find("RENDER 30US") != std::string::npos &&
                hud_lines[2].find("BREAKS T1 B2 C3") != std::string::npos,
            "profiler HUD third line should include render timings and break reasons");
    }
}

} // namespace

int main() {
    int failures = 0;
    failures += run_test("Interpolation Edge Cases", test_interpolation_edges);
    failures += run_test("Matrix Composition", test_matrix_composition);
    failures += run_test("Topological Bone Reorder", test_topological_bone_reorder);
    failures += run_test(
        "SIMD World Transform Propagation",
        test_simd_world_transform_propagation);
    failures += run_test("IK Solving", test_ik_cases);
    failures += run_test("Physics Stepping", test_physics_stepping);
    failures += run_test("SkeletonBounds Queries", test_skeleton_bounds_queries);
    failures += run_test("Custom Allocator Lifecycle", test_custom_allocator_lifecycle);
    failures += run_test("AnimationState Snapshot Restore", test_animation_state_snapshot_restore);
    failures += run_test("Animation Layers", test_animation_layers);
    failures += run_test("Concave Stencil Clipping", test_concave_stencil_clipping);
    failures += run_test("Nested Stencil Restoration", test_nested_stencil_reference_restoration);
    failures += run_test(
        "Binary Key Quantization And Reduction",
        test_binary_key_quantization_and_reduction);
    failures += run_test("Runtime Profiler Frame", test_runtime_profiler_frame);

    if (failures != 0) {
        std::cerr << failures << " unit test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "All runtime and renderer unit tests passed.\n";
    return 0;
}
