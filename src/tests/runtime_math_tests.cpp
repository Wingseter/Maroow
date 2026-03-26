#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "module_internal.hpp"
#include "marrow/runtime/animation_compare.hpp"
#include "marrow/runtime/animation.hpp"
#include "marrow/runtime/skeleton.hpp"
#include "skeleton_internal.hpp"

namespace {

using marrow::runtime::AttachmentData;
using marrow::runtime::AttachmentKind;
using marrow::runtime::AttachmentVertex;
using marrow::runtime::BoneData;
using marrow::runtime::BoneInherit;
using marrow::runtime::BonePose;
using marrow::runtime::BoneRotateTimeline;
using marrow::runtime::BoneTransform;
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
    std::vector<SkinData> skins = {}) {
    SkeletonInfo info;
    info.name = "runtime_math_unit_tests";
    return std::make_shared<SkeletonData>(
        std::move(info),
        std::move(bones),
        std::move(ik_constraints),
        std::vector<marrow::runtime::PathConstraintData>{},
        std::vector<marrow::runtime::TransformConstraintData>{},
        std::move(physics_constraints),
        std::move(slots),
        std::vector<marrow::runtime::EventDefinition>{},
        std::vector<marrow::runtime::AnimationData>{},
        std::move(skins),
        0.0,
        std::vector<marrow::runtime::AnimationMixDefinition>{});
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

} // namespace

int main() {
    int failures = 0;
    failures += run_test("Interpolation Edge Cases", test_interpolation_edges);
    failures += run_test("Matrix Composition", test_matrix_composition);
    failures += run_test("IK Solving", test_ik_cases);
    failures += run_test("Physics Stepping", test_physics_stepping);
    failures += run_test("SkeletonBounds Queries", test_skeleton_bounds_queries);
    failures += run_test("Concave Stencil Clipping", test_concave_stencil_clipping);
    failures += run_test("Nested Stencil Restoration", test_nested_stencil_reference_restoration);
    failures += run_test(
        "Binary Key Quantization And Reduction",
        test_binary_key_quantization_and_reduction);

    if (failures != 0) {
        std::cerr << failures << " unit test assertion(s) failed.\n";
        return 1;
    }

    std::cout << "All runtime and renderer unit tests passed.\n";
    return 0;
}
