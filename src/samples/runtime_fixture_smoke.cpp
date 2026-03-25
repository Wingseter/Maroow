#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/json.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace {

using marrow::runtime::AtlasData;
using marrow::runtime::AtlasLoader;
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::Value;
using marrow::runtime::Skeleton;
using marrow::runtime::SkeletonData;

bool print_error(const LoadError& error) {
    std::cerr << error.format() << '\n';
    return false;
}

bool require_near(double actual, double expected, std::string_view label) {
    constexpr double kTolerance = 1e-6;
    if (std::abs(actual - expected) <= kTolerance) {
        return true;
    }

    std::cerr << label << " expected " << expected << " but got " << actual << ".\n";
    return false;
}

std::string object_path(std::string_view base, std::size_t index) {
    return std::string(base) + "[" + std::to_string(index) + "]";
}

bool require_non_empty_array(
    const Document& document,
    const Value& array_value,
    std::string_view json_path) {
    if (const auto error = marrow::runtime::json::require_type(
            document, array_value, Value::Type::Array, json_path)) {
        return print_error(*error);
    }

    if (array_value.as_array().empty()) {
        return print_error(marrow::runtime::json::make_validation_error(
            document,
            array_value.location(),
            std::string(json_path),
            "array must not be empty"));
    }

    return true;
}

bool validate_fixture_skeleton(const Document& document) {
    const Value& root = document.root;
    if (const auto error = marrow::runtime::json::require_type(
            document, root, Value::Type::Object, "$")) {
        return print_error(*error);
    }

    const Value* skeleton = nullptr;
    const Value* bones = nullptr;
    const Value* slots = nullptr;
    const Value* animations = nullptr;

    if (const auto error = marrow::runtime::json::require_member(
            document, root, "marrow", Value::Type::String, "$")) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "skeleton", Value::Type::Object, "$", &skeleton)) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "bones", Value::Type::Array, "$", &bones)) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "slots", Value::Type::Array, "$", &slots)) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, root, "animations", Value::Type::Object, "$", &animations)) {
        return print_error(*error);
    }

    if (const auto error = marrow::runtime::json::require_member(
            document, *skeleton, "name", Value::Type::String, "$.skeleton")) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, *skeleton, "width", Value::Type::Number, "$.skeleton")) {
        return print_error(*error);
    }
    if (const auto error = marrow::runtime::json::require_member(
            document, *skeleton, "height", Value::Type::Number, "$.skeleton")) {
        return print_error(*error);
    }

    if (!require_non_empty_array(document, *bones, "$.bones")) {
        return false;
    }
    for (std::size_t index = 0; index < bones->as_array().size(); ++index) {
        const Value& bone = bones->as_array()[index];
        const std::string path = object_path("$.bones", index);
        if (const auto error = marrow::runtime::json::require_type(
                document, bone, Value::Type::Object, path)) {
            return print_error(*error);
        }
        if (const auto error = marrow::runtime::json::require_member(
                document, bone, "name", Value::Type::String, path)) {
            return print_error(*error);
        }
    }

    if (!require_non_empty_array(document, *slots, "$.slots")) {
        return false;
    }
    for (std::size_t index = 0; index < slots->as_array().size(); ++index) {
        const Value& slot = slots->as_array()[index];
        const std::string path = object_path("$.slots", index);
        if (const auto error = marrow::runtime::json::require_type(
                document, slot, Value::Type::Object, path)) {
            return print_error(*error);
        }
        if (const auto error = marrow::runtime::json::require_member(
                document, slot, "name", Value::Type::String, path)) {
            return print_error(*error);
        }
        if (const auto error = marrow::runtime::json::require_member(
                document, slot, "bone", Value::Type::String, path)) {
            return print_error(*error);
        }
        if (const auto error = marrow::runtime::json::require_member(
                document, slot, "attachment", Value::Type::String, path)) {
            return print_error(*error);
        }
    }

    if (animations->as_object().empty()) {
        return print_error(marrow::runtime::json::make_validation_error(
            document,
            animations->location(),
            "$.animations",
            "object must not be empty"));
    }

    std::cout << "Loaded skeleton fixture: " << document.source_path.string() << '\n'
              << "  bones=" << bones->as_array().size() << ", slots=" << slots->as_array().size()
              << ", animations=" << animations->as_object().size() << '\n';
    return true;
}

std::optional<Document> load_document_or_print(const std::filesystem::path& path) {
    const auto result = marrow::runtime::json::load_document(path);
    if (!result) {
        print_error(*result.error);
        return std::nullopt;
    }
    return *result.document;
}

bool validate_runtime_skeleton_model(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    Skeleton skeleton(skeleton_data);

    if (skeleton_data->bones().size() != 3 || skeleton_data->slots().size() != 2 ||
        skeleton_data->animations().size() != 1) {
        std::cerr << "SkeletonData did not preserve fixture counts.\n";
        return false;
    }

    if (skeleton_data->bones()[1].parent_index != std::optional<std::size_t>{0} ||
        skeleton_data->slots()[0].bone_index != 1 ||
        skeleton_data->animations()[0].targeted_bone_indices != std::vector<std::size_t>{1}) {
        std::cerr << "SkeletonData relationships do not match the fixture.\n";
        return false;
    }

    if (skeleton.bone_poses().size() != skeleton_data->bones().size() ||
        skeleton.bone_world_transforms().size() != skeleton_data->bones().size() ||
        skeleton.slot_states().size() != skeleton_data->slots().size() ||
        skeleton.draw_order().size() != skeleton_data->slots().size()) {
        std::cerr << "Skeleton instance state does not match SkeletonData sizes.\n";
        return false;
    }

    if (!require_near(skeleton.bone_world_transforms()[1].world_x, 0.0, "spine setup world_x") ||
        !require_near(skeleton.bone_world_transforms()[1].world_y, 50.0, "spine setup world_y") ||
        !require_near(skeleton.bone_world_transforms()[2].world_x, -30.0, "arm_l setup world_x") ||
        !require_near(skeleton.bone_world_transforms()[2].world_y, 60.0, "arm_l setup world_y")) {
        std::cerr << "Setup-pose hierarchy evaluation did not match the fixture chain.\n";
        return false;
    }

    skeleton.bone_poses()[0].local_pose.x = 10.0;
    skeleton.bone_poses()[1].local_pose.y = 72.0;
    skeleton.slot_states()[0].attachment_name = "debug_attachment";
    skeleton.draw_order()[0] = 1;
    skeleton.draw_order()[1] = 0;
    skeleton.update_world_transforms();

    if (!require_near(skeleton.bone_world_transforms()[2].world_x, -20.0, "arm_l mutated world_x") ||
        !require_near(skeleton.bone_world_transforms()[2].world_y, 82.0, "arm_l mutated world_y")) {
        std::cerr << "Mutated hierarchy evaluation did not propagate parent transforms.\n";
        return false;
    }

    skeleton.set_to_setup_pose();

    if (skeleton.bone_poses()[0].local_pose.x != 0.0 ||
        skeleton.bone_poses()[1].local_pose.y != 50.0 ||
        skeleton.slot_states()[0].attachment_name != "body" ||
        skeleton.draw_order() != std::vector<std::size_t>{0, 1}) {
        std::cerr << "Skeleton did not reset to setup pose.\n";
        return false;
    }

    if (skeleton_data->bones()[0].setup_pose.x != 0.0 ||
        skeleton_data->bones()[1].setup_pose.y != 50.0 ||
        skeleton_data->slots()[0].setup_attachment != "body") {
        std::cerr << "Mutable instance state leaked back into SkeletonData.\n";
        return false;
    }

    if (!require_near(skeleton.bone_world_transforms()[2].world_x, -30.0, "arm_l reset world_x") ||
        !require_near(skeleton.bone_world_transforms()[2].world_y, 60.0, "arm_l reset world_y")) {
        std::cerr << "Reset setup pose did not restore hierarchy world transforms.\n";
        return false;
    }

    std::cout << "Loaded runtime skeleton model: " << skeleton_data->info().name << '\n'
              << "  bones=" << skeleton_data->bones().size()
              << ", slots=" << skeleton_data->slots().size()
              << ", animations=" << skeleton_data->animations().size() << '\n'
              << "  resetPoseY=" << skeleton.bone_poses()[1].local_pose.y
              << ", armWorld=(" << skeleton.bone_world_transforms()[2].world_x << ", "
              << skeleton.bone_world_transforms()[2].world_y << ")\n";
    return true;
}

bool validate_runtime_animation_curves(const std::shared_ptr<const SkeletonData>& skeleton_data) {
    if (skeleton_data->animations().empty()) {
        std::cerr << "Fixture did not load any animations.\n";
        return false;
    }

    const auto spine_index = skeleton_data->find_bone_index("spine");
    if (!spine_index.has_value()) {
        std::cerr << "Fixture lost the spine bone index.\n";
        return false;
    }

    const auto& animation = skeleton_data->animations()[0];
    const marrow::runtime::BoneRotateTimeline* rotate_timeline =
        animation.find_rotate_timeline(*spine_index);
    if (rotate_timeline == nullptr || rotate_timeline->keyframes.size() != 3) {
        std::cerr << "Rotate timeline parsing did not preserve the fixture keyframes.\n";
        return false;
    }

    if (rotate_timeline->keyframes[0].interpolation.kind() !=
            marrow::runtime::InterpolationKind::Linear ||
        rotate_timeline->keyframes[1].interpolation.kind() !=
            marrow::runtime::InterpolationKind::CubicBezier ||
        rotate_timeline->keyframes[2].interpolation.kind() !=
            marrow::runtime::InterpolationKind::Stepped) {
        std::cerr << "Rotate timeline interpolation kinds do not match the fixture curves.\n";
        return false;
    }

    const marrow::runtime::CubicBezierControlPoints& bezier =
        rotate_timeline->keyframes[1].interpolation.cubic_bezier();
    if (!require_near(bezier.cx1, 0.25, "rotate bezier cx1") ||
        !require_near(bezier.cy1, 0.1, "rotate bezier cy1") ||
        !require_near(bezier.cx2, 0.75, "rotate bezier cx2") ||
        !require_near(bezier.cy2, 0.9, "rotate bezier cy2")) {
        std::cerr << "Rotate timeline bezier control points did not round-trip.\n";
        return false;
    }

    const std::optional<double> linear_sample = animation.sample_bone_rotation(*spine_index, 0.25);
    const std::optional<double> bezier_sample = animation.sample_bone_rotation(*spine_index, 0.625);
    const std::optional<double> start_sample = animation.sample_bone_rotation(*spine_index, 0.0);
    const std::optional<double> midpoint_sample = animation.sample_bone_rotation(*spine_index, 0.5);
    const std::optional<double> end_sample = animation.sample_bone_rotation(*spine_index, 1.0);
    const double stepped_mid = marrow::runtime::interpolate_value(
        5.0,
        10.0,
        rotate_timeline->keyframes[2].interpolation,
        0.5);
    const double stepped_end = marrow::runtime::interpolate_value(
        5.0,
        10.0,
        rotate_timeline->keyframes[2].interpolation,
        1.0);

    if (!linear_sample.has_value() || !bezier_sample.has_value() ||
        !start_sample.has_value() || !midpoint_sample.has_value() || !end_sample.has_value()) {
        std::cerr << "Animation sampling did not find the rotate timeline.\n";
        return false;
    }

    if (!require_near(*linear_sample, 2.5, "linear rotate sample") ||
        !require_near(*bezier_sample, 3.9529315894941443, "bezier rotate sample") ||
        !require_near(stepped_mid, 5.0, "stepped rotate sample mid") ||
        !require_near(stepped_end, 10.0, "stepped rotate sample end") ||
        !require_near(*start_sample, 0.0, "rotate start sample") ||
        !require_near(*midpoint_sample, 5.0, "rotate midpoint sample") ||
        !require_near(*end_sample, 0.0, "rotate final sample")) {
        std::cerr << "Interpolation evaluators did not reproduce the fixture timeline.\n";
        return false;
    }

    std::cout << "Loaded runtime animation curves: " << animation.name << '\n'
              << "  rotateKeys=" << rotate_timeline->keyframes.size()
              << ", linear@0.25=" << *linear_sample
              << ", bezier@0.625=" << *bezier_sample
              << ", steppedMid=" << stepped_mid << '\n';
    return true;
}

bool validate_runtime_atlas_model(
    const Document& atlas_document,
    const std::shared_ptr<const SkeletonData>& skeleton_data) {
    const auto atlas_result = AtlasLoader::load(atlas_document);
    if (!atlas_result) {
        return print_error(*atlas_result.error);
    }

    const std::shared_ptr<const AtlasData>& atlas_data = atlas_result.atlas_data;
    if (atlas_data->info().name != "player_fixture" ||
        atlas_data->info().image != "player_fixture.png" ||
        !require_near(atlas_data->info().width, 256.0, "atlas width") ||
        !require_near(atlas_data->info().height, 256.0, "atlas height") ||
        atlas_data->info().filter_min != "linear" ||
        atlas_data->info().filter_mag != "linear" ||
        atlas_data->info().wrap_x != "clamp_to_edge" ||
        atlas_data->info().wrap_y != "clamp_to_edge") {
        std::cerr << "AtlasData did not preserve atlas metadata.\n";
        return false;
    }

    if (atlas_data->regions().size() != 2) {
        std::cerr << "AtlasData did not preserve fixture region count.\n";
        return false;
    }

    const auto* body_region = atlas_data->find_region("body");
    const auto* arm_region = atlas_data->find_region("arm_l");
    const auto* body_attachment_region =
        atlas_data->find_region_for_attachment(skeleton_data->slots()[0].setup_attachment);
    const auto* arm_attachment_region =
        atlas_data->find_region_for_attachment(skeleton_data->slots()[1].setup_attachment);

    if (body_region == nullptr || arm_region == nullptr ||
        body_attachment_region == nullptr || arm_attachment_region == nullptr) {
        std::cerr << "Atlas region lookup failed for fixture names.\n";
        return false;
    }

    if (body_attachment_region != body_region || arm_attachment_region != arm_region) {
        std::cerr << "Attachment lookup did not resolve the same atlas regions as direct lookup.\n";
        return false;
    }

    if (!require_near(body_region->x, 0.0, "body region x") ||
        !require_near(body_region->y, 0.0, "body region y") ||
        !require_near(body_region->width, 128.0, "body region width") ||
        !require_near(body_region->height, 160.0, "body region height") ||
        !require_near(body_region->origin_x, 64.0, "body region origin_x") ||
        !require_near(body_region->origin_y, 80.0, "body region origin_y") ||
        !require_near(arm_region->x, 128.0, "arm_l region x") ||
        !require_near(arm_region->y, 0.0, "arm_l region y") ||
        !require_near(arm_region->width, 64.0, "arm_l region width") ||
        !require_near(arm_region->height, 96.0, "arm_l region height") ||
        !require_near(arm_region->origin_x, 16.0, "arm_l region origin_x") ||
        !require_near(arm_region->origin_y, 72.0, "arm_l region origin_y")) {
        std::cerr << "AtlasData regions did not preserve fixture geometry.\n";
        return false;
    }

    if (atlas_data->find_region("missing_region") != nullptr ||
        atlas_data->find_region_for_attachment("missing_attachment") != nullptr) {
        std::cerr << "Atlas lookup should return null for unknown regions.\n";
        return false;
    }

    std::cout << "Loaded runtime atlas model: " << atlas_data->info().name << '\n'
              << "  image=" << atlas_data->info().image
              << ", regions=" << atlas_data->regions().size() << '\n'
              << "  bodyOrigin=(" << body_region->origin_x << ", " << body_region->origin_y << ")"
              << ", armAttachment=" << skeleton_data->slots()[1].setup_attachment << '\n';
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path skeleton_path =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("assets/fixtures/player_idle.mskl");
    const std::filesystem::path atlas_path =
        argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("assets/fixtures/player_idle.matl");

    const std::optional<Document> skeleton_document = load_document_or_print(skeleton_path);
    const std::optional<Document> atlas_document = load_document_or_print(atlas_path);
    if (!skeleton_document.has_value() || !atlas_document.has_value()) {
        return 1;
    }

    const bool skeleton_ok = validate_fixture_skeleton(*skeleton_document);
    const auto skeleton_result = marrow::runtime::load_skeleton_data(*skeleton_document);
    if (!skeleton_result) {
        print_error(*skeleton_result.error);
        return 1;
    }

    const bool runtime_skeleton_ok = validate_runtime_skeleton_model(skeleton_result.skeleton_data);
    const bool runtime_animation_ok = validate_runtime_animation_curves(skeleton_result.skeleton_data);
    const bool runtime_atlas_ok =
        validate_runtime_atlas_model(*atlas_document, skeleton_result.skeleton_data);
    if (!skeleton_ok || !runtime_skeleton_ok || !runtime_animation_ok || !runtime_atlas_ok) {
        return 1;
    }

    std::cout << "Fixture JSON smoke test passed.\n";
    return 0;
}
