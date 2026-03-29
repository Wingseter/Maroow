#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
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
using marrow::runtime::AtlasData;
using marrow::runtime::AtlasInfo;
using marrow::runtime::AtlasRegion;
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
using marrow::runtime::TransformConstraintData;
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

double precise_cubic_component(double control_point_1, double control_point_2, double t) {
    const double inverse_t = 1.0 - t;
    return 3.0 * inverse_t * inverse_t * t * control_point_1 +
        3.0 * inverse_t * t * t * control_point_2 + t * t * t;
}

double precise_cubic_derivative(double control_point_1, double control_point_2, double t) {
    const double inverse_t = 1.0 - t;
    return 3.0 * inverse_t * inverse_t * control_point_1 +
        6.0 * inverse_t * t * (control_point_2 - control_point_1) +
        3.0 * t * t * (1.0 - control_point_2);
}

double precise_bezier_transform(const marrow::runtime::Interpolation& interpolation, double alpha) {
    const auto& bezier = interpolation.cubic_bezier();
    double t = std::clamp(alpha, 0.0, 1.0);
    for (int iteration = 0; iteration < 8; ++iteration) {
        const double x =
            precise_cubic_component(bezier.cx1, bezier.cx2, t) - alpha;
        if (std::abs(x) <= 1e-7) {
            break;
        }
        const double derivative = precise_cubic_derivative(bezier.cx1, bezier.cx2, t);
        if (std::abs(derivative) <= 1e-7) {
            break;
        }
        t = std::clamp(t - x / derivative, 0.0, 1.0);
    }
    return precise_cubic_component(bezier.cy1, bezier.cy2, t);
}

double normalize_rotation_degrees(double angle) {
    return angle - (std::ceil(angle / 360.0 - 0.5) * 360.0);
}

double world_rotation_degrees(const BoneWorldTransform& transform) {
    return radians_to_degrees(std::atan2(transform.c, transform.a));
}

void expect_less_equal(
    TestContext& context,
    double actual,
    double limit,
    std::string_view message) {
    if (actual <= limit) {
        return;
    }

    ++context.failures;
    std::cerr << context.name << ": " << message << " expected <= " << limit
              << " but got " << actual << ".\n";
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

std::shared_ptr<const SkeletonData> make_skeleton_data_with_constraints(
    std::vector<BoneData> bones,
    std::vector<IkConstraintData> ik_constraints,
    std::vector<TransformConstraintData> transform_constraints) {
    SkeletonInfo info;
    info.name = "constraint_dirty_skip_tests";
    return marrow::allocate_shared<SkeletonData>(
        std::move(info),
        std::move(bones),
        std::move(ik_constraints),
        std::vector<marrow::runtime::PathConstraintData>{},
        std::move(transform_constraints),
        std::vector<PhysicsConstraintData>{},
        std::vector<SlotData>{},
        std::vector<marrow::runtime::EventDefinition>{},
        std::vector<AnimationData>{},
        std::vector<SkinData>{},
        0.0,
        std::vector<AnimationMixDefinition>{});
}

std::vector<BoneWorldTransform> collect_world_transforms(const Skeleton& skeleton) {
    std::vector<BoneWorldTransform> world_transforms;
    world_transforms.reserve(skeleton.bone_world_transforms().size());
    for (const BoneWorldTransform world : skeleton.bone_world_transforms()) {
        world_transforms.push_back(world);
    }
    return world_transforms;
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

AttachmentData make_region_attachment(
    std::string name,
    std::string region_name) {
    AttachmentData attachment;
    attachment.name = std::move(name);
    attachment.kind = AttachmentKind::Region;
    attachment.region_name = std::move(region_name);
    return attachment;
}

AttachmentData make_mesh_attachment(
    std::string name,
    std::string region_name) {
    AttachmentData attachment;
    attachment.name = std::move(name);
    attachment.kind = AttachmentKind::Mesh;
    attachment.region_name = std::move(region_name);

    marrow::runtime::MeshGeometry geometry;
    geometry.vertices = {
        -16.0, -16.0,
        16.0, -16.0,
        16.0, 16.0,
        -16.0, 16.0,
    };
    geometry.uvs = {
        0.0, 0.0,
        1.0, 0.0,
        1.0, 1.0,
        0.0, 1.0,
    };
    geometry.triangles = {0U, 1U, 2U, 0U, 2U, 3U};
    geometry.weights.resize(4U);
    geometry.weights[0].influences.push_back({0U, -16.0, -16.0, 1.0});
    geometry.weights[1].influences.push_back({1U, 4.0, -16.0, 1.0});
    geometry.weights[2].influences.push_back({1U, 4.0, 16.0, 1.0});
    geometry.weights[3].influences.push_back({0U, -16.0, 16.0, 1.0});
    attachment.mesh_geometry =
        marrow::allocate_shared<marrow::runtime::MeshGeometry>(std::move(geometry));
    return attachment;
}

AttachmentData make_clipping_attachment(
    std::string name,
    std::string end_slot_name,
    std::optional<std::size_t> end_slot_index,
    std::vector<AttachmentVertex> polygon) {
    AttachmentData attachment;
    attachment.name = std::move(name);
    attachment.kind = AttachmentKind::Clipping;
    attachment.clipping_attachment = marrow::runtime::ClippingAttachmentData{
        std::move(polygon),
        end_slot_index,
        std::move(end_slot_name),
    };
    return attachment;
}

std::shared_ptr<const AtlasData> make_renderer_test_atlas() {
    AtlasInfo info;
    info.name = "renderer_cache_test_atlas";
    info.image = "renderer_cache_test.png";
    info.width = 256.0;
    info.height = 256.0;
    info.filter_min = "linear";
    info.filter_mag = "linear";
    info.wrap_x = "clamp";
    info.wrap_y = "clamp";

    std::vector<AtlasRegion> regions;
    for (const std::string_view region_name : {
             std::string_view("body_default"),
             std::string_view("body_swap"),
             std::string_view("body_alt"),
             std::string_view("body_mesh"),
             std::string_view("arm_default"),
         }) {
        AtlasRegion region;
        region.name = std::string(region_name);
        region.width = 32.0;
        region.height = 32.0;
        region.origin_x = 16.0;
        region.origin_y = 16.0;
        regions.push_back(std::move(region));
    }

    return marrow::allocate_shared<AtlasData>(std::move(info), std::move(regions));
}

std::shared_ptr<const SkeletonData> make_renderer_cache_test_data() {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone("arm", 0, BoneTransform{12.0, 4.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    SlotData body_slot;
    body_slot.name = "body";
    body_slot.bone_index = 0;
    body_slot.setup_attachment = "body_default";

    SlotData arm_slot;
    arm_slot.name = "arm";
    arm_slot.bone_index = 1;
    arm_slot.setup_attachment = "arm_default";

    SkinData default_skin;
    default_skin.name = "default";
    default_skin.slot_attachments.push_back({0U, make_region_attachment("body_default", "body_default")});
    default_skin.slot_attachments.push_back({0U, make_region_attachment("body_swap", "body_swap")});
    default_skin.slot_attachments.push_back({1U, make_region_attachment("arm_default", "arm_default")});

    SkinData alt_skin;
    alt_skin.name = "alt";
    alt_skin.slot_attachments.push_back({0U, make_region_attachment("body_alt", "body_alt")});

    return make_skeleton_data(
        std::move(bones),
        {},
        {},
        {body_slot, arm_slot},
        {default_skin, alt_skin});
}

std::shared_ptr<const SkeletonData> make_renderer_mesh_cache_test_data(bool include_clip_slot) {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone("arm", 0U, BoneTransform{12.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    std::vector<SlotData> slots;
    if (include_clip_slot) {
        SlotData clip_slot;
        clip_slot.name = "mask";
        clip_slot.bone_index = 0U;
        clip_slot.setup_attachment = "mesh_mask";
        slots.push_back(std::move(clip_slot));
    }

    SlotData body_slot;
    body_slot.name = "body";
    body_slot.bone_index = 0U;
    body_slot.setup_attachment = "body_mesh";
    slots.push_back(std::move(body_slot));

    SkinData default_skin;
    default_skin.name = "default";
    if (include_clip_slot) {
        default_skin.slot_attachments.push_back({
            0U,
            make_clipping_attachment(
                "mesh_mask",
                "body",
                1U,
                {
                    {-10.0, -10.0},
                    {10.0, -10.0},
                    {10.0, 10.0},
                    {-10.0, 10.0},
                })});
    }
    default_skin.slot_attachments.push_back({
        include_clip_slot ? 1U : 0U,
        make_mesh_attachment("body_mesh", "body_mesh")});

    return make_skeleton_data(
        std::move(bones),
        {},
        {},
        std::move(slots),
        {std::move(default_skin)});
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

std::shared_ptr<const SkeletonData> make_constraint_allocation_test_data() {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone(
        "follower_a",
        0,
        BoneTransform{6.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "follower_b",
        1,
        BoneTransform{6.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    SlotData slot;
    slot.name = "guide";
    slot.bone_index = 0U;
    slot.setup_attachment = "guide_path";

    AttachmentData path_attachment;
    path_attachment.name = "guide_path";
    path_attachment.kind = AttachmentKind::Path;
    path_attachment.path_attachment = marrow::runtime::PathAttachmentData{
        {
            {0.0, 0.0},
            {8.0, 10.0},
            {16.0, -10.0},
            {24.0, 0.0},
            {32.0, 10.0},
            {40.0, -10.0},
            {48.0, 0.0},
        }};

    SkinData default_skin;
    default_skin.name = "default";
    default_skin.slot_attachments.push_back({0U, std::move(path_attachment)});

    marrow::runtime::PathConstraintData path_constraint;
    path_constraint.name = "follow";
    path_constraint.slot_index = 0U;
    path_constraint.bone_indices = {1U, 2U};
    path_constraint.position = 0.05;
    path_constraint.spacing = 0.35;
    path_constraint.spacing_mode = marrow::runtime::PathConstraintSpacingMode::Percent;
    path_constraint.rotate_mix = 1.0;
    path_constraint.translate_mix = 1.0;

    SkeletonInfo info;
    info.name = "constraint_allocation_test";
    return marrow::allocate_shared<SkeletonData>(
        std::move(info),
        std::move(bones),
        std::vector<IkConstraintData>{},
        std::vector<marrow::runtime::PathConstraintData>{std::move(path_constraint)},
        std::vector<marrow::runtime::TransformConstraintData>{},
        std::vector<PhysicsConstraintData>{},
        std::vector<SlotData>{std::move(slot)},
        std::vector<marrow::runtime::EventDefinition>{},
        std::vector<AnimationData>{},
        std::vector<SkinData>{std::move(default_skin)},
        0.0,
        std::vector<AnimationMixDefinition>{});
}

std::shared_ptr<const SkeletonData> make_constraint_dirty_skip_test_data() {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));

    bones.push_back(make_bone("upper_a", 0, BoneTransform{}));
    bones.push_back(make_bone(
        "lower_a",
        1,
        BoneTransform{5.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "hand_a",
        2,
        BoneTransform{5.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "target_a",
        0,
        BoneTransform{6.0, 8.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    bones.push_back(make_bone(
        "upper_b",
        0,
        BoneTransform{0.0, 20.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "lower_b",
        5,
        BoneTransform{5.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "hand_b",
        6,
        BoneTransform{5.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "target_b",
        0,
        BoneTransform{6.0, 28.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    bones.push_back(make_bone(
        "source_a",
        0,
        BoneTransform{20.0, 0.0, 18.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "target_ta",
        0,
        BoneTransform{25.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "target_tb",
        0,
        BoneTransform{25.0, 5.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    bones.push_back(make_bone(
        "source_b",
        0,
        BoneTransform{20.0, 20.0, -12.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "target_tc",
        0,
        BoneTransform{25.0, 20.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone(
        "target_td",
        0,
        BoneTransform{25.0, 25.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    IkConstraintData ik_a;
    ik_a.name = "ik_a";
    ik_a.bone_indices = {1U, 2U};
    ik_a.target_bone_index = 4U;
    ik_a.mix = 1.0;

    IkConstraintData ik_b;
    ik_b.name = "ik_b";
    ik_b.bone_indices = {5U, 6U};
    ik_b.target_bone_index = 8U;
    ik_b.mix = 1.0;

    TransformConstraintData transform_a;
    transform_a.name = "transform_a";
    transform_a.source_bone_index = 9U;
    transform_a.target_bone_indices = {10U, 11U};
    transform_a.rotate_mix = 1.0;
    transform_a.translate_mix = 1.0;
    transform_a.offsets.rotation = 5.0;
    transform_a.offsets.x = 2.0;
    transform_a.offsets.y = -1.0;

    TransformConstraintData transform_b;
    transform_b.name = "transform_b";
    transform_b.source_bone_index = 12U;
    transform_b.target_bone_indices = {13U, 14U};
    transform_b.rotate_mix = 1.0;
    transform_b.translate_mix = 1.0;
    transform_b.offsets.rotation = -7.0;
    transform_b.offsets.x = -1.5;
    transform_b.offsets.y = 2.5;

    return make_skeleton_data_with_constraints(
        std::move(bones),
        std::vector<IkConstraintData>{std::move(ik_a), std::move(ik_b)},
        std::vector<TransformConstraintData>{std::move(transform_a), std::move(transform_b)});
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

void test_animation_float_storage_and_constant_pruning(TestContext& context) {
    context.expect(
        (std::is_same_v<decltype(marrow::runtime::CubicBezierControlPoints{}.cx1), float>),
        "cubic bezier control points should store float32");
    context.expect(
        (std::is_same_v<decltype(RotateKeyframe{}.time), float>),
        "rotate keyframe times should store float32");
    context.expect(
        (std::is_same_v<decltype(RotateKeyframe{}.angle), float>),
        "rotate keyframe values should store float32");
    context.expect(
        (std::is_same_v<decltype(marrow::runtime::VectorKeyframe{}.x), float>),
        "vector keyframe values should store float32");
    context.expect(
        (std::is_same_v<decltype(marrow::runtime::ColorKeyframe{}.time), float>),
        "color keyframe times should store float32");
    context.expect(
        (std::is_same_v<
            decltype(marrow::runtime::DeformKeyframe{}.vertex_offsets)::value_type,
            float>),
        "deform keyframe offsets should store float32");
    context.expect(
        (std::is_same_v<
            decltype(marrow::runtime::EventKeyframe{}.float_value)::value_type,
            float>),
        "event keyframe float overrides should store float32");

    const Interpolation bezier = Interpolation::cubic_bezier(0.25, 0.10, 0.75, 0.90);
    for (int index = 0; index <= 32; ++index) {
        const double alpha = static_cast<double>(index) / 32.0;
        context.expect_near(
            bezier.transform(alpha),
            precise_bezier_transform(bezier, alpha),
            "cubic bezier LUT should stay close to the precise solver",
            5e-4);
    }

    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone(
        "arm",
        0U,
        BoneTransform{5.0, -2.0, 10.0, 1.5, 0.75, 3.0, -4.0}));

    SlotData slot;
    slot.name = "body";
    slot.bone_index = 1U;
    slot.setup_attachment = "body";
    slot.color = marrow::runtime::SlotColor{0.8, 0.7, 0.6, 1.0};

    SkinData default_skin;
    default_skin.name = "default";
    default_skin.slot_attachments.push_back({0U, make_region_attachment("body", "body")});

    AnimationData animation;
    animation.name = "pruned";
    animation.targeted_bone_indices = {0U, 1U};
    animation.bone_rotate_timelines.push_back(BoneRotateTimeline{
        0U,
        0.0,
        {RotateKeyframe{0.0, 15.0, Interpolation::linear()}}});
    animation.bone_rotate_timelines.push_back(BoneRotateTimeline{
        1U,
        10.0,
        {RotateKeyframe{0.0, 0.0, Interpolation::linear()}}});
    animation.bone_inherit_timelines.push_back(marrow::runtime::BoneInheritTimeline{
        1U,
        {marrow::runtime::InheritKeyframe{0.0, BoneInherit::Normal}}});
    animation.bone_translate_timelines.push_back(BoneTranslateTimeline{
        1U,
        {marrow::runtime::VectorKeyframe{0.0, 5.0, -2.0, Interpolation::linear()}}});
    animation.bone_scale_timelines.push_back(marrow::runtime::BoneScaleTimeline{
        1U,
        {marrow::runtime::VectorKeyframe{0.0, 1.5, 0.75, Interpolation::linear()}}});
    animation.bone_shear_timelines.push_back(marrow::runtime::BoneShearTimeline{
        1U,
        {marrow::runtime::VectorKeyframe{0.0, 3.0, -4.0, Interpolation::linear()}}});

    marrow::runtime::SlotAttachmentTimeline attachment_timeline;
    attachment_timeline.slot_index = 0U;
    attachment_timeline.keyframes.push_back(
        marrow::runtime::AttachmentKeyframe{0.0, std::optional<std::string>{"body"}});
    animation.slot_attachment_timelines.push_back(std::move(attachment_timeline));

    marrow::runtime::SlotColorTimeline color_timeline;
    color_timeline.slot_index = 0U;
    marrow::runtime::ColorKeyframe color_keyframe;
    color_keyframe.time = 0.0f;
    color_keyframe.color = slot.color;
    color_keyframe.interpolation = Interpolation::linear();
    color_timeline.keyframes.push_back(color_keyframe);
    animation.slot_color_timelines.push_back(std::move(color_timeline));

    marrow::runtime::DrawOrderTimeline draw_order_timeline;
    draw_order_timeline.keyframes.push_back(marrow::runtime::DrawOrderKeyframe{0.0f, {0U}});
    animation.draw_order_timeline_data = std::move(draw_order_timeline);

    const auto skeleton_data = make_skeleton_data(
        std::move(bones),
        {},
        {},
        {slot},
        {default_skin},
        {animation});
    const AnimationData* loaded_animation = skeleton_data->find_animation("pruned");
    context.expect(loaded_animation != nullptr, "constant-track animation should load");
    if (loaded_animation == nullptr) {
        return;
    }

    context.expect(
        loaded_animation->bone_rotate_timelines.size() == 1U,
        "identity rotate timeline should prune while active rotate stays");
    context.expect(
        loaded_animation->bone_inherit_timelines.empty(),
        "identity inherit timeline should prune");
    context.expect(
        loaded_animation->bone_translate_timelines.empty(),
        "identity translate timeline should prune");
    context.expect(
        loaded_animation->bone_scale_timelines.empty(),
        "identity scale timeline should prune");
    context.expect(
        loaded_animation->bone_shear_timelines.empty(),
        "identity shear timeline should prune");
    context.expect(
        loaded_animation->slot_attachment_timelines.empty(),
        "identity attachment timeline should prune");
    context.expect(
        loaded_animation->slot_color_timelines.empty(),
        "identity color timeline should prune");
    context.expect(
        !loaded_animation->draw_order_timeline_data.has_value(),
        "identity draw-order timeline should prune");
    context.expect(
        loaded_animation->targeted_bone_indices == std::vector<std::size_t>{0U},
        "targeted bones should rebuild from the remaining active timelines");

    const std::optional<double> sampled_rotation = loaded_animation->sample_bone_rotation(0U, 0.5);
    context.expect(sampled_rotation.has_value(), "remaining rotate timeline should still sample");
    if (sampled_rotation.has_value()) {
        context.expect_near(*sampled_rotation, 15.0, "remaining rotate timeline value");
    }
}

void test_animation_timeline_index_and_sampling_cursor(TestContext& context) {
    AnimationData animation;
    animation.name = "cached";
    animation.targeted_bone_indices = {1U};
    animation.bone_rotate_timelines.push_back(BoneRotateTimeline{
        1U,
        0.0,
        {
            RotateKeyframe{0.0, 0.0, Interpolation::linear()},
            RotateKeyframe{0.5, 60.0, Interpolation::linear()},
            RotateKeyframe{1.0, 120.0, Interpolation::linear()},
        }});
    animation.bone_inherit_timelines.push_back(marrow::runtime::BoneInheritTimeline{
        1U,
        {
            marrow::runtime::InheritKeyframe{0.0, BoneInherit::Normal},
            marrow::runtime::InheritKeyframe{0.5, BoneInherit::OnlyTranslation},
        }});
    animation.bone_translate_timelines.push_back(BoneTranslateTimeline{
        1U,
        {
            marrow::runtime::VectorKeyframe{0.0, 0.0, 0.0, Interpolation::linear()},
            marrow::runtime::VectorKeyframe{0.5, 8.0, 4.0, Interpolation::linear()},
            marrow::runtime::VectorKeyframe{1.0, 16.0, 8.0, Interpolation::linear()},
        }});
    animation.bone_scale_timelines.push_back(marrow::runtime::BoneScaleTimeline{
        1U,
        {
            marrow::runtime::VectorKeyframe{0.0, 1.0, 1.0, Interpolation::linear()},
            marrow::runtime::VectorKeyframe{1.0, 1.5, 0.75, Interpolation::linear()},
        }});
    animation.bone_shear_timelines.push_back(marrow::runtime::BoneShearTimeline{
        1U,
        {
            marrow::runtime::VectorKeyframe{0.0, 0.0, 0.0, Interpolation::linear()},
            marrow::runtime::VectorKeyframe{1.0, 12.0, -6.0, Interpolation::linear()},
        }});

    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone("arm", 0U));

    const auto skeleton_data = make_skeleton_data(std::move(bones), {}, {}, {}, {}, {animation});
    const AnimationData* loaded_animation = skeleton_data->find_animation("cached");
    context.expect(loaded_animation != nullptr, "cached animation should load into SkeletonData");
    if (loaded_animation == nullptr) {
        return;
    }

    context.expect(
        loaded_animation->bone_timeline_index.size() == skeleton_data->bones().size(),
        "bone timeline index should allocate one entry per bone");
    context.expect(
        loaded_animation->find_rotate_timeline(1U) == &loaded_animation->bone_rotate_timelines[0],
        "rotate lookup should hit the indexed timeline entry");
    context.expect(
        loaded_animation->find_inherit_timeline(1U) == &loaded_animation->bone_inherit_timelines[0],
        "inherit lookup should hit the indexed timeline entry");
    context.expect(
        loaded_animation->find_translate_timeline(1U) ==
            &loaded_animation->bone_translate_timelines[0],
        "translate lookup should hit the indexed timeline entry");
    context.expect(
        loaded_animation->find_scale_timeline(1U) == &loaded_animation->bone_scale_timelines[0],
        "scale lookup should hit the indexed timeline entry");
    context.expect(
        loaded_animation->find_shear_timeline(1U) == &loaded_animation->bone_shear_timelines[0],
        "shear lookup should hit the indexed timeline entry");
    context.expect(
        loaded_animation->find_rotate_timeline(0U) == nullptr,
        "untimed bones should not return indexed transform timelines");

    marrow::runtime::SamplingContext sampling_context;
    const std::optional<double> early_sample =
        loaded_animation->sample_bone_rotation(1U, 0.25, &sampling_context);
    const std::optional<double> late_sample =
        loaded_animation->sample_bone_rotation(1U, 0.75, &sampling_context);
    context.expect(early_sample.has_value(), "cached cursor should sample early rotation");
    context.expect(late_sample.has_value(), "cached cursor should sample late rotation");
    if (early_sample.has_value()) {
        context.expect_near(*early_sample, 30.0, "early cached rotation sample");
    }
    if (late_sample.has_value()) {
        context.expect_near(*late_sample, 90.0, "late cached rotation sample");
    }
    context.expect(
        sampling_context.rotate_last_keyframe_indices.size() == 1U &&
            sampling_context.rotate_last_keyframe_indices[0] == 1U,
        "forward sampling should advance the cached rotate cursor");

    const std::optional<double> looped_sample =
        loaded_animation->sample_bone_rotation(1U, 0.10, &sampling_context);
    context.expect(looped_sample.has_value(), "rewound cached rotation sample should succeed");
    if (looped_sample.has_value()) {
        context.expect_near(*looped_sample, 12.0, "rewound cached rotation sample");
    }
    context.expect(
        sampling_context.rotate_last_keyframe_indices.size() == 1U &&
            sampling_context.rotate_last_keyframe_indices[0] == 0U,
        "rewinding the sample time should reset the cached rotate cursor");
}

void test_constraint_fast_math_approximations(TestContext& context) {
    constexpr float kPiF = 3.14159265358979323846f;
    constexpr float kTwoPiF = kPiF * 2.0f;
    constexpr float kAtanToleranceRadians = 0.005f;
    constexpr float kTrigTolerance = 0.0012f;
    constexpr float kAcosTolerance = 1e-4f;
    constexpr float kSqrtRelativeTolerance = 1e-4f;

    const auto normalize_radians = [](float angle) {
        angle = std::fmod(angle + kPiF, kTwoPiF);
        if (angle < 0.0f) {
            angle += kTwoPiF;
        }
        return angle - kPiF;
    };

    float max_atan_error = 0.0f;
    for (int yi = -256; yi <= 256; ++yi) {
        for (int xi = -256; xi <= 256; ++xi) {
            if (xi == 0 && yi == 0) {
                continue;
            }

            const float x = static_cast<float>(xi) / 17.0f;
            const float y = static_cast<float>(yi) / 19.0f;
            const float error = std::abs(normalize_radians(
                std::atan2(y, x) - marrow::runtime::detail::fast_atan2f(y, x)));
            max_atan_error = std::max(max_atan_error, error);
        }
    }
    expect_less_equal(
        context,
        max_atan_error,
        kAtanToleranceRadians,
        "fast_atan2f max error should stay below 0.005 radians");

    float max_sin_error = 0.0f;
    float max_cos_error = 0.0f;
    for (int step = 0; step <= 4096; ++step) {
        const float angle =
            -4.0f * kPiF + (8.0f * kPiF * static_cast<float>(step) / 4096.0f);
        max_sin_error = std::max(
            max_sin_error,
            std::abs(std::sin(angle) - marrow::runtime::detail::fast_sinf(angle)));
        max_cos_error = std::max(
            max_cos_error,
            std::abs(std::cos(angle) - marrow::runtime::detail::fast_cosf(angle)));
    }
    expect_less_equal(
        context,
        max_sin_error,
        kTrigTolerance,
        "fast_sinf max error should stay below the sampled tolerance");
    expect_less_equal(
        context,
        max_cos_error,
        kTrigTolerance,
        "fast_cosf max error should stay below the sampled tolerance");

    float max_acos_error = 0.0f;
    for (int step = 0; step <= 4096; ++step) {
        const float value = -1.0f + (2.0f * static_cast<float>(step) / 4096.0f);
        max_acos_error = std::max(
            max_acos_error,
            std::abs(std::acos(value) - marrow::runtime::detail::fast_acosf(value)));
    }
    expect_less_equal(
        context,
        max_acos_error,
        kAcosTolerance,
        "fast_acosf max error should stay below the sampled tolerance");

    float max_sqrt_relative_error = 0.0f;
    for (int step = 0; step <= 4096; ++step) {
        const float value = 1.0e-6f + (1024.0f * static_cast<float>(step) / 4096.0f);
        const float precise = std::sqrt(value);
        const float approximate = marrow::runtime::detail::fast_sqrtf(value);
        max_sqrt_relative_error = std::max(
            max_sqrt_relative_error,
            std::abs(approximate - precise) / precise);
    }
    expect_less_equal(
        context,
        max_sqrt_relative_error,
        kSqrtRelativeTolerance,
        "fast_sqrtf relative error should stay below the sampled tolerance");
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
    context.expect(
        skeleton_data->children_map().size() == 3U,
        "children_map should cache one adjacency list per reordered bone");
    if (skeleton_data->children_map().size() == 3U) {
        context.expect(
            skeleton_data->children_map()[0] == std::vector<std::size_t>{1U},
            "root children should remap to the reordered arm");
        context.expect(
            skeleton_data->children_map()[1] == std::vector<std::size_t>{2U},
            "arm children should remap to the reordered hand");
        context.expect(
            skeleton_data->children_map()[2].empty(),
            "leaf bones should cache an empty child list");
    }
    context.expect(
        skeleton_data->bone_tip_local_vectors().size() == 3U,
        "bone tip cache should include one entry per reordered bone");
    if (skeleton_data->bone_tip_local_vectors().size() == 3U) {
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[0].x,
            2.0,
            "root tip cache x should follow the reordered arm");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[0].y,
            0.0,
            "root tip cache y should follow the reordered arm");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[1].x,
            5.0,
            "arm tip cache x should follow the reordered hand");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[1].y,
            0.0,
            "arm tip cache y should follow the reordered hand");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[2].x,
            0.0,
            "leaf tip cache x should default to zero");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[2].y,
            0.0,
            "leaf tip cache y should default to zero");
    }

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

void test_skeleton_data_children_map_and_tip_cache(TestContext& context) {
    std::vector<BoneData> bones;
    bones.push_back(make_bone("root", std::nullopt));
    bones.push_back(make_bone("near", 0, BoneTransform{1.0, 2.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone("far", 0, BoneTransform{-3.0, 4.0, 0.0, 1.0, 1.0, 0.0, 0.0}));
    bones.push_back(make_bone("far_tip", 2, BoneTransform{2.0, 1.0, 0.0, 1.0, 1.0, 0.0, 0.0}));

    const auto skeleton_data = make_skeleton_data(std::move(bones));

    context.expect(
        skeleton_data->children_map().size() == 4U,
        "children_map should size-match the authored bone list");
    if (skeleton_data->children_map().size() == 4U) {
        context.expect(
            skeleton_data->children_map()[0] == std::vector<std::size_t>({1U, 2U}),
            "root should cache both direct children");
        context.expect(
            skeleton_data->children_map()[1].empty(),
            "near should cache an empty child list");
        context.expect(
            skeleton_data->children_map()[2] == std::vector<std::size_t>{3U},
            "far should cache its grandchild edge");
        context.expect(
            skeleton_data->children_map()[3].empty(),
            "far_tip should cache an empty child list");
    }

    context.expect(
        skeleton_data->bone_tip_local_vectors().size() == 4U,
        "bone tip cache should size-match the authored bone list");
    if (skeleton_data->bone_tip_local_vectors().size() == 4U) {
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[0].x,
            -3.0,
            "root tip cache should prefer the furthest direct child x");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[0].y,
            4.0,
            "root tip cache should prefer the furthest direct child y");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[2].x,
            2.0,
            "far tip cache should follow its direct child x");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[2].y,
            1.0,
            "far tip cache should follow its direct child y");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[3].x,
            0.0,
            "leaf tip cache x should default to zero");
        context.expect_near(
            skeleton_data->bone_tip_local_vectors()[3].y,
            0.0,
            "leaf tip cache y should default to zero");
    }
}

void test_simd_world_transform_propagation(TestContext& context) {
    constexpr double kSkeletonScaleX = 1.37;
    constexpr double kSkeletonScaleY = 0.83;
    context.expect(
        sizeof(BoneWorldTransform) == sizeof(float) * 6U,
        "world transform storage should pack down to six floats");

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
        kSkeletonScaleX,
        kSkeletonScaleY,
        local_buffers,
        scalar_world);
    marrow::runtime::detail::propagate_world_transforms_optimized(
        bones,
        poses,
        kSkeletonScaleX,
        kSkeletonScaleY,
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

void test_constraint_hot_path_allocations(TestContext& context) {
    const auto skeleton_data = make_constraint_allocation_test_data();
    Skeleton skeleton(skeleton_data);

    skeleton.update_world_transforms(PhysicsMode::None);
    for (std::size_t frame = 0; frame < 5U; ++frame) {
        skeleton.bone_poses()[0].local_pose.rotation = static_cast<double>(frame) * 4.0;
        skeleton.bone_poses()[0].local_pose.x = static_cast<double>(frame) * 0.75;
        const std::size_t allocation_count_before = skeleton.constraint_allocation_count();
        skeleton.update_world_transforms(PhysicsMode::None);
        const std::size_t allocation_count_after = skeleton.constraint_allocation_count();
        context.expect(
            allocation_count_after == allocation_count_before,
            "constraint hot path should not grow scratch buffers after warmup");
    }
}

void test_constraint_dirty_skip_preserves_output(TestContext& context) {
    Skeleton skeleton(make_constraint_dirty_skip_test_data());

    marrow::runtime::WorldTransformTimingBreakdown initial_timing;
    skeleton.update_world_transforms(PhysicsMode::None, &initial_timing);
    const std::vector<BoneWorldTransform> initial_world = collect_world_transforms(skeleton);

    marrow::runtime::WorldTransformTimingBreakdown skipped_timing;
    skeleton.update_world_transforms(PhysicsMode::None, &skipped_timing);
    const std::vector<BoneWorldTransform> skipped_world = collect_world_transforms(skeleton);

    context.expect(
        skipped_timing.evaluated_ik_constraints == 0U &&
            skipped_timing.skipped_ik_constraints == 2U,
        "unchanged IK constraints should reuse the cached solve output");
    context.expect(
        skipped_timing.evaluated_transform_constraints == 0U &&
            skipped_timing.skipped_transform_constraints == 2U,
        "unchanged transform constraints should reuse the cached solve output");
    context.expect(
        initial_world.size() == skipped_world.size(),
        "constraint dirty skip regression should preserve the world-transform buffer size");

    if (initial_world.size() != skipped_world.size()) {
        return;
    }

    for (std::size_t bone_index = 0; bone_index < initial_world.size(); ++bone_index) {
        expect_world_transform(
            context,
            skipped_world[bone_index],
            initial_world[bone_index],
            "constraint dirty skip world");
    }
}

void test_constraint_dirty_skip_re_evaluates_only_affected_constraints(TestContext& context) {
    Skeleton skeleton(make_constraint_dirty_skip_test_data());
    skeleton.update_world_transforms(PhysicsMode::None);
    const std::vector<BoneWorldTransform> baseline_world = collect_world_transforms(skeleton);

    skeleton.bone_poses()[4].local_pose.y = -6.0;
    marrow::runtime::WorldTransformTimingBreakdown ik_timing;
    skeleton.update_world_transforms(PhysicsMode::None, &ik_timing);
    const std::vector<BoneWorldTransform> ik_world = collect_world_transforms(skeleton);

    context.expect(
        ik_timing.evaluated_ik_constraints == 1U &&
            ik_timing.skipped_ik_constraints == 1U,
        "moving one IK target should only re-evaluate the affected IK constraint");
    context.expect(
        ik_timing.evaluated_transform_constraints == 0U &&
            ik_timing.skipped_transform_constraints == 2U,
        "moving an IK target should leave transform constraints on the cached skip path");
    context.expect(
        std::abs(ik_world[3].world_y - baseline_world[3].world_y) > 0.5,
        "the affected IK branch should move after its target changes");
    context.expect_near(
        ik_world[7].world_x,
        baseline_world[7].world_x,
        "the unaffected IK branch should keep its cached world x");
    context.expect_near(
        ik_world[7].world_y,
        baseline_world[7].world_y,
        "the unaffected IK branch should keep its cached world y");

    skeleton.bone_poses()[12].local_pose.rotation -= 20.0;
    marrow::runtime::WorldTransformTimingBreakdown transform_timing;
    skeleton.update_world_transforms(PhysicsMode::None, &transform_timing);
    const std::vector<BoneWorldTransform> transform_world = collect_world_transforms(skeleton);

    context.expect(
        transform_timing.evaluated_ik_constraints == 0U &&
            transform_timing.skipped_ik_constraints == 2U,
        "changing only a transform source should keep both IK constraints on the cached path");
    context.expect(
        transform_timing.evaluated_transform_constraints == 1U &&
            transform_timing.skipped_transform_constraints == 1U,
        "changing one transform source should only re-evaluate the affected transform constraint");
    context.expect(
        std::abs(transform_world[13].a - ik_world[13].a) > 0.05 ||
            std::abs(transform_world[13].c - ik_world[13].c) > 0.05 ||
            std::abs(transform_world[13].world_x - ik_world[13].world_x) > 0.5 ||
            std::abs(transform_world[13].world_y - ik_world[13].world_y) > 0.5,
        "the affected transform branch should change after its source changes");
    context.expect_near(
        transform_world[10].world_x,
        ik_world[10].world_x,
        "the unaffected transform branch should keep its cached world x");
    context.expect_near(
        transform_world[10].world_y,
        ik_world[10].world_y,
        "the unaffected transform branch should keep its cached world y");
}

void test_ik_cases(TestContext& context) {
    constexpr double kConstraintPositionTolerance = 0.05;
    constexpr double kConstraintAngleToleranceDegrees = 0.3;

    Skeleton reachable(make_two_bone_ik_data(6.0, 8.0, true));
    const BoneWorldTransform& reachable_hand = reachable.bone_world_transforms()[3];
    context.expect_near(
        reachable_hand.world_x,
        6.0,
        "reachable ik hand x",
        kConstraintPositionTolerance);
    context.expect_near(
        reachable_hand.world_y,
        8.0,
        "reachable ik hand y",
        kConstraintPositionTolerance);

    Skeleton unreachable(make_two_bone_ik_data(20.0, 0.0, true));
    const BoneWorldTransform& unreachable_hand = unreachable.bone_world_transforms()[3];
    context.expect_near(
        unreachable_hand.world_x,
        10.0,
        "unreachable ik hand x",
        kConstraintPositionTolerance);
    context.expect_near(
        unreachable_hand.world_y,
        0.0,
        "unreachable ik hand y",
        kConstraintPositionTolerance);

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
    context.expect_near(
        wrap_rotation,
        0.0,
        "wrap-around one-bone ik uses shortest arc",
        kConstraintAngleToleranceDegrees);
    context.expect_near(
        wrap.bone_world_transforms()[2].world_x,
        10.0,
        "wrap-around one-bone muzzle x",
        kConstraintPositionTolerance);
    context.expect_near(
        wrap.bone_world_transforms()[2].world_y,
        0.0,
        "wrap-around one-bone muzzle y",
        kConstraintPositionTolerance);
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

void test_dynamic_mesh_cache_static_payload_and_deform_updates(TestContext& context) {
    const auto skeleton_data = make_renderer_mesh_cache_test_data(false);
    const auto atlas_data = make_renderer_test_atlas();
    Skeleton skeleton(skeleton_data);
    skeleton.update_world_transforms(PhysicsMode::Pose);

    marrow::renderer::PreparedSceneCache cache;
    const marrow::renderer::PreparedSceneCacheResult initial_result =
        marrow::renderer::prepare_setup_pose_scene_cached(
            &cache,
            skeleton,
            *atlas_data);
    context.expect(static_cast<bool>(initial_result), initial_result.error_message);
    if (!initial_result) {
        return;
    }

    context.expect(
        cache.slot_records_.size() == 1U,
        "mesh cache test should create one cached slot record");
    if (cache.slot_records_.size() != 1U || !cache.slot_records_[0].draw_command.has_value()) {
        return;
    }

    auto* initial_mesh = std::get_if<marrow::renderer::DynamicMeshDrawCommand>(
        &(*cache.slot_records_[0].draw_command));
    context.expect(initial_mesh != nullptr, "cached slot record should store a mesh draw command");
    if (initial_mesh == nullptr) {
        return;
    }

    const auto* initial_payload_data = initial_mesh->vertex_payloads.data();
    const double initial_payload_x = initial_mesh->vertex_payloads[0].bone_local_positions[0].x;
    context.expect(
        initial_mesh->vertex_buffer_usage == marrow::renderer::MeshBufferUsage::Static,
        "mesh payload should use a static vertex buffer");
    context.expect(
        !initial_mesh->deform_buffer_usage.has_value(),
        "mesh deform buffer should stay disabled without active offsets");
    context.expect(
        initial_mesh->masked_vertices.empty() &&
            initial_mesh->masked_indices.empty() &&
            initial_mesh->has_bounds,
        "unclipped mesh cache entries should skip CPU-skinned masked geometry");

    skeleton.bone_poses()[0].local_pose.x = 18.0;
    skeleton.update_world_transforms(PhysicsMode::Pose);
    const marrow::renderer::PreparedSceneCacheResult pose_only_result =
        marrow::renderer::prepare_setup_pose_scene_cached(
            &cache,
            skeleton,
            *atlas_data);
    context.expect(static_cast<bool>(pose_only_result), pose_only_result.error_message);
    context.expect(
        pose_only_result.update_info != nullptr &&
            pose_only_result.update_info->cache_hit &&
            pose_only_result.update_info->bone_palette_only,
        "pose-only mesh updates should hit the bone-palette-only cache path");

    auto* pose_only_mesh = std::get_if<marrow::renderer::DynamicMeshDrawCommand>(
        &(*cache.slot_records_[0].draw_command));
    context.expect(
        pose_only_mesh != nullptr &&
            pose_only_mesh->vertex_payloads.data() == initial_payload_data,
        "pose-only updates should reuse the cached static mesh payload");

    skeleton.mesh_deform_states()[0].attachment_name = "body_mesh";
    skeleton.mesh_deform_states()[0].vertex_offsets = {
        1.0, -2.0,
        2.5, -1.0,
        2.5, 1.0,
        1.0, 2.0,
    };
    const marrow::renderer::PreparedSceneCacheResult deform_result =
        marrow::renderer::prepare_setup_pose_scene_cached(
            &cache,
            skeleton,
            *atlas_data);
    context.expect(static_cast<bool>(deform_result), deform_result.error_message);
    context.expect(
        deform_result.update_info != nullptr &&
            deform_result.update_info->dirty_slot_count == 1U,
        "deform edits should dirty the cached mesh slot");

    auto* deformed_mesh = std::get_if<marrow::renderer::DynamicMeshDrawCommand>(
        &(*cache.slot_records_[0].draw_command));
    context.expect(deformed_mesh != nullptr, "deform rebuild should preserve the cached mesh record");
    if (deformed_mesh == nullptr) {
        return;
    }

    context.expect(
        deformed_mesh->vertex_payloads.data() == initial_payload_data,
        "deform-only updates should keep the static vertex payload allocation");
    context.expect_near(
        deformed_mesh->vertex_payloads[0].bone_local_positions[0].x,
        initial_payload_x,
        "deform-only updates should not rewrite bind-pose mesh payloads");
    context.expect(
        deformed_mesh->deform_buffer_usage == marrow::renderer::MeshBufferUsage::Dynamic,
        "deform-only updates should activate the dynamic deform buffer");
    context.expect(
        deformed_mesh->deform_offsets.size() == deformed_mesh->vertex_payloads.size(),
        "deform offsets should upload one x/y pair per cached mesh vertex");
    if (deformed_mesh->deform_offsets.size() == deformed_mesh->vertex_payloads.size()) {
        context.expect_near(
            deformed_mesh->deform_offsets[0].x,
            1.0,
            "deform offsets should preserve the authored x delta");
        context.expect_near(
            deformed_mesh->deform_offsets[0].y,
            -2.0,
            "deform offsets should preserve the authored y delta");
    }
}

void test_dynamic_mesh_clipping_uses_stencil_only(TestContext& context) {
    const auto skeleton_data = make_renderer_mesh_cache_test_data(true);
    const auto atlas_data = make_renderer_test_atlas();
    Skeleton skeleton(skeleton_data);
    skeleton.update_world_transforms(PhysicsMode::Pose);

    marrow::renderer::PreparedSceneCache cache;
    const marrow::renderer::PreparedSceneCacheResult cache_result =
        marrow::renderer::prepare_setup_pose_scene_cached(
            &cache,
            skeleton,
            *atlas_data);
    context.expect(static_cast<bool>(cache_result), cache_result.error_message);
    if (!cache_result || cache.scene().draw_commands.empty()) {
        return;
    }

    const auto* clipped_mesh = marrow::renderer::dynamic_mesh_attachment_command(
        cache.scene().draw_commands.back());
    context.expect(clipped_mesh != nullptr, "clipped mesh cache test should keep a mesh draw command");
    if (clipped_mesh == nullptr) {
        return;
    }

    context.expect(
        clipped_mesh->clip_attachment_name == std::optional<std::string>{"mesh_mask"},
        "cached clipped meshes should preserve the active clip attachment name");
    context.expect(
        clipped_mesh->masked_vertices.empty() &&
            clipped_mesh->masked_indices.empty() &&
            clipped_mesh->has_bounds,
        "clipped meshes should keep stencil metadata without CPU-clipped geometry");

    constexpr std::array<float, 16> kIdentityProjection{{
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    }};
    const marrow::renderer::RenderCommandListResult command_list_result =
        marrow::renderer::build_render_command_list(cache.scene(), kIdentityProjection);
    context.expect(static_cast<bool>(command_list_result), command_list_result.error_message);
    if (!command_list_result || cache.scene().clip_attachments.size() != 1U) {
        return;
    }

    const marrow::renderer::RenderCommandList& command_list =
        *command_list_result.command_list;
    context.expect(
        command_list.commands.size() == 1U &&
            command_list.clip_commands.size() == 1U &&
            command_list.commands[0].vertices.size() == clipped_mesh->vertex_payloads.size() &&
            command_list.commands[0].indices.size() == clipped_mesh->indices.size(),
        "render command list should keep the original mesh geometry when clipping");
    context.expect(
        command_list.ordered_events.size() == 3U &&
            command_list.ordered_events[0].kind ==
                marrow::renderer::RenderCommandEventKind::ClipStart &&
            command_list.ordered_events[1].kind ==
                marrow::renderer::RenderCommandEventKind::Draw &&
            command_list.ordered_events[2].kind ==
                marrow::renderer::RenderCommandEventKind::ClipEnd,
        "render command list should use clip start/draw/end events for the clipped mesh");

    const marrow::renderer::GpuSkinningEvaluationResult skinned_result =
        marrow::renderer::evaluate_gpu_skinned_vertices(
            *clipped_mesh,
            cache.scene().bone_palette);
    context.expect(static_cast<bool>(skinned_result), skinned_result.error_message.value_or(""));
    if (!skinned_result) {
        return;
    }

    renderer_internal::SoftwareStencilBuffer buffer;
    if (const std::optional<std::string> error =
            renderer_internal::initialize_software_stencil_buffer(
                48,
                48,
                -24.0,
                -24.0,
                1.0,
                &buffer)) {
        context.expect(false, *error);
        return;
    }

    const std::optional<renderer_internal::SoftwareStencilClipState> clip_state =
        renderer_internal::stencil_clip_state_for_depth(1U);
    context.expect(clip_state.has_value(), "mesh clip test should allocate a stencil reference");
    if (!clip_state.has_value()) {
        return;
    }

    if (const std::optional<std::string> error =
            renderer_internal::apply_software_stencil_clip_push(
                cache.scene().clip_attachments.front().polygon,
                *clip_state,
                &buffer)) {
        context.expect(false, *error);
        return;
    }

    std::size_t visible_pixels = 0U;
    for (std::size_t index = 0; index + 2U < clipped_mesh->indices.size(); index += 3U) {
        const std::size_t i0 = clipped_mesh->indices[index];
        const std::size_t i1 = clipped_mesh->indices[index + 1U];
        const std::size_t i2 = clipped_mesh->indices[index + 2U];
        if (i0 >= skinned_result.vertices.size() ||
            i1 >= skinned_result.vertices.size() ||
            i2 >= skinned_result.vertices.size()) {
            continue;
        }

        visible_pixels += renderer_internal::count_software_stencil_visible_pixels(
            buffer,
            {
                skinned_result.vertices[i0].position,
                skinned_result.vertices[i1].position,
                skinned_result.vertices[i2].position,
            },
            clip_state->reference_value);
    }
    context.expect(
        visible_pixels == 400U,
        "stencil-only clipped mesh should preserve the previous visible coverage");
}

void test_prepared_scene_cache_dirty_updates(TestContext& context) {
    const auto skeleton_data = make_renderer_cache_test_data();
    const auto atlas_data = make_renderer_test_atlas();
    Skeleton skeleton(skeleton_data);
    skeleton.update_world_transforms(PhysicsMode::Pose);

    marrow::renderer::PreparedSceneCache cache;
    const auto attachment_names = [](const marrow::renderer::PreparedScene& scene) {
        std::vector<std::string> names;
        names.reserve(scene.draw_commands.size());
        for (const auto& command : scene.draw_commands) {
            const auto* region = marrow::renderer::region_attachment_command(command);
            if (region != nullptr) {
                names.push_back(region->attachment_name);
            }
        }
        return names;
    };

    const marrow::renderer::PreparedSceneCacheResult initial_result =
        marrow::renderer::prepare_setup_pose_scene_cached(
            &cache,
            skeleton,
            *atlas_data);
    context.expect(static_cast<bool>(initial_result), initial_result.error_message);
    if (!initial_result) {
        return;
    }

    const marrow::renderer::PreparedSceneBatchSummary* initial_summary =
        marrow::renderer::summarize_prepared_scene_batches_cached(&cache);
    context.expect(initial_summary != nullptr, "prepared scene cache should produce a batch summary");
    context.expect(
        initial_summary != nullptr && static_cast<bool>(*initial_summary),
        initial_summary != nullptr && initial_summary->error_message.has_value()
            ? *initial_summary->error_message
            : "prepared scene cache batch summary should succeed");
    context.expect(
        initial_result.update_info != nullptr &&
            initial_result.update_info->rebuilt_slot_count == 2U,
        "initial prepared scene cache build should rebuild both visible slots");
    context.expect(
        attachment_names(*initial_result.scene) ==
            std::vector<std::string>({"body_default", "arm_default"}),
        "initial prepared scene cache should preserve setup attachments");

    skeleton.bone_poses()[0].local_pose.x = 24.0;
    skeleton.update_world_transforms(PhysicsMode::Pose);
    const marrow::renderer::PreparedSceneCacheResult palette_only_result =
        marrow::renderer::prepare_setup_pose_scene_cached(
            &cache,
            skeleton,
            *atlas_data);
    context.expect(static_cast<bool>(palette_only_result), palette_only_result.error_message);
    context.expect(
        palette_only_result.update_info != nullptr &&
            palette_only_result.update_info->cache_hit &&
            palette_only_result.update_info->bone_palette_only,
        "bone-only pose changes should hit the prepared-scene cache");
    context.expect(
        std::find(cache.slot_dirty_flags().begin(), cache.slot_dirty_flags().end(), true) ==
            cache.slot_dirty_flags().end(),
        "bone-only pose changes should leave slot dirty flags clear");
    context.expect(
        std::abs(cache.scene().bone_palette[0].world_x - skeleton.bone_world_transforms()[0].world_x) <=
            kTolerance,
        "bone-only cache hits should refresh the cached bone palette");

    skeleton.slot_states()[0].attachment_name = "body_swap";
    skeleton.slot_states()[0].attachment_skin_index = 0U;
    const marrow::renderer::PreparedSceneCacheResult attachment_swap_result =
        marrow::renderer::prepare_setup_pose_scene_cached(
            &cache,
            skeleton,
            *atlas_data);
    context.expect(static_cast<bool>(attachment_swap_result), attachment_swap_result.error_message);
    context.expect(
        attachment_swap_result.update_info != nullptr &&
            attachment_swap_result.update_info->dirty_slot_count == 1U &&
            cache.slot_dirty_flags().size() >= 1U &&
            cache.slot_dirty_flags()[0],
        "attachment swaps should mark the affected slot dirty");
    context.expect(
        attachment_names(cache.scene()) == std::vector<std::string>({"body_swap", "arm_default"}),
        "attachment swaps should rebuild the affected cached slot");

    std::swap(skeleton.draw_order()[0], skeleton.draw_order()[1]);
    const marrow::renderer::PreparedSceneCacheResult draw_order_result =
        marrow::renderer::prepare_setup_pose_scene_cached(
            &cache,
            skeleton,
            *atlas_data);
    context.expect(static_cast<bool>(draw_order_result), draw_order_result.error_message);
    context.expect(
        cache.draw_order_dirty() &&
            draw_order_result.update_info != nullptr &&
            draw_order_result.update_info->draw_order_dirty,
        "draw-order edits should mark the prepared-scene cache dirty");
    context.expect(
        attachment_names(cache.scene()) == std::vector<std::string>({"arm_default", "body_swap"}),
        "draw-order cache updates should preserve the new slot order");

    skeleton.draw_order()[0] = 0U;
    skeleton.draw_order()[1] = 1U;
    const bool skin_applied = skeleton.set_skin("alt");
    context.expect(skin_applied, "renderer cache test should activate the alternate skin");
    const marrow::renderer::PreparedSceneCacheResult skin_swap_result =
        marrow::renderer::prepare_setup_pose_scene_cached(
            &cache,
            skeleton,
            *atlas_data);
    context.expect(static_cast<bool>(skin_swap_result), skin_swap_result.error_message);
    context.expect(
        cache.skin_swap_dirty() &&
            skin_swap_result.update_info != nullptr &&
            skin_swap_result.update_info->skin_swap_dirty,
        "skin swaps should flag the prepared-scene cache skin dirty state");
    context.expect(
        attachment_names(cache.scene()) == std::vector<std::string>({"body_alt", "arm_default"}),
        "skin swaps should rebuild the affected cached attachment");
}

} // namespace

int main() {
    int failures = 0;
    failures += run_test("Interpolation Edge Cases", test_interpolation_edges);
    failures += run_test(
        "Constraint Fast Math Approximations",
        test_constraint_fast_math_approximations);
    failures += run_test(
        "Animation Float Storage And Constant Pruning",
        test_animation_float_storage_and_constant_pruning);
    failures += run_test(
        "Animation Timeline Index And Sampling Cursor",
        test_animation_timeline_index_and_sampling_cursor);
    failures += run_test("Matrix Composition", test_matrix_composition);
    failures += run_test("Topological Bone Reorder", test_topological_bone_reorder);
    failures += run_test(
        "SkeletonData Children Map And Tip Cache",
        test_skeleton_data_children_map_and_tip_cache);
    failures += run_test(
        "SIMD World Transform Propagation",
        test_simd_world_transform_propagation);
    failures += run_test(
        "Constraint Hot Path Allocations",
        test_constraint_hot_path_allocations);
    failures += run_test(
        "Constraint Dirty Skip Preserves Output",
        test_constraint_dirty_skip_preserves_output);
    failures += run_test(
        "Constraint Dirty Skip Re-evaluates Only Affected Constraints",
        test_constraint_dirty_skip_re_evaluates_only_affected_constraints);
    failures += run_test("IK Solving", test_ik_cases);
    failures += run_test("Physics Stepping", test_physics_stepping);
    failures += run_test("SkeletonBounds Queries", test_skeleton_bounds_queries);
    failures += run_test("Custom Allocator Lifecycle", test_custom_allocator_lifecycle);
    failures += run_test("AnimationState Snapshot Restore", test_animation_state_snapshot_restore);
    failures += run_test("Animation Layers", test_animation_layers);
    failures += run_test("Concave Stencil Clipping", test_concave_stencil_clipping);
    failures += run_test("Nested Stencil Restoration", test_nested_stencil_reference_restoration);
    failures += run_test(
        "Dynamic Mesh Cache Static Payload And Deform Updates",
        test_dynamic_mesh_cache_static_payload_and_deform_updates);
    failures += run_test(
        "Dynamic Mesh Clipping Uses Stencil Only",
        test_dynamic_mesh_clipping_uses_stencil_only);
    failures += run_test(
        "PreparedScene Cache Dirty Updates",
        test_prepared_scene_cache_dirty_updates);
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
