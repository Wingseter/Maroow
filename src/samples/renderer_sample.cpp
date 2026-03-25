#include <cmath>
#include <filesystem>
#include <iostream>
#include <string_view>

#include "marrow/renderer/module.hpp"
#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace {

bool require_near(double actual, double expected, std::string_view label) {
    constexpr double kTolerance = 1e-6;
    if (std::abs(actual - expected) <= kTolerance) {
        return true;
    }

    std::cerr << label << " expected " << expected << " but got " << actual << ".\n";
    return false;
}

bool validate_attachment(
    const marrow::renderer::RegionAttachmentDrawCommand& attachment,
    std::string_view expected_slot_name,
    std::string_view expected_attachment_name,
    std::string_view expected_region_name,
    std::size_t expected_bone_index,
    double expected_min_x,
    double expected_min_y,
    double expected_max_x,
    double expected_max_y,
    double expected_u_min,
    double expected_v_min,
    double expected_u_max,
    double expected_v_max) {
    if (attachment.slot_name != expected_slot_name ||
        attachment.attachment_name != expected_attachment_name ||
        attachment.atlas_region_name != expected_region_name ||
        attachment.bone_index != expected_bone_index) {
        std::cerr << "Attachment identity mismatch for slot " << expected_slot_name << ".\n";
        return false;
    }

    return require_near(attachment.vertices[0].position.x, expected_min_x, "quad min x") &&
        require_near(attachment.vertices[0].position.y, expected_min_y, "quad min y") &&
        require_near(attachment.vertices[2].position.x, expected_max_x, "quad max x") &&
        require_near(attachment.vertices[2].position.y, expected_max_y, "quad max y") &&
        require_near(attachment.vertices[0].uv.x, expected_u_min, "uv min u") &&
        require_near(attachment.vertices[0].uv.y, expected_v_min, "uv min v") &&
        require_near(attachment.vertices[2].uv.x, expected_u_max, "uv max u") &&
        require_near(attachment.vertices[2].uv.y, expected_v_max, "uv max v");
}

} // namespace

int main(int argc, char** argv) {
    const std::filesystem::path skeleton_path =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::path("assets/fixtures/player_idle.mskl");
    const std::filesystem::path atlas_path =
        argc > 2 ? std::filesystem::path(argv[2]) : std::filesystem::path("assets/fixtures/player_idle.matl");

    const auto skeleton_result = marrow::runtime::load_skeleton_data(skeleton_path);
    if (!skeleton_result) {
        std::cerr << skeleton_result.error->format() << '\n';
        return 1;
    }

    const auto atlas_result = marrow::runtime::AtlasLoader::load(atlas_path);
    if (!atlas_result) {
        std::cerr << atlas_result.error->format() << '\n';
        return 1;
    }

    marrow::runtime::Skeleton skeleton(skeleton_result.skeleton_data);
    skeleton.set_to_setup_pose();

    const auto scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return 1;
    }

    const marrow::renderer::PreparedScene& scene = *scene_result.scene;
    if (scene.region_attachments.size() != 2) {
        std::cerr << "Expected 2 setup-pose region attachments but prepared "
                  << scene.region_attachments.size() << ".\n";
        return 1;
    }

    const bool body_ok = validate_attachment(
        scene.region_attachments[0],
        "body",
        "body",
        "body",
        1,
        -64.0,
        -30.0,
        64.0,
        130.0,
        0.0,
        0.0,
        0.5,
        0.625);
    const bool arm_ok = validate_attachment(
        scene.region_attachments[1],
        "arm_l",
        "arm_l",
        "arm_l",
        2,
        -46.0,
        -12.0,
        18.0,
        84.0,
        0.5,
        0.0,
        0.75,
        0.375);
    if (!body_ok || !arm_ok) {
        std::cerr << "Setup-pose region attachment placement did not match the fixture transforms.\n";
        return 1;
    }

    marrow::renderer::SampleAppWindow window;
    window.title = "Marrow Render Validation";
    window.width = 1280;
    window.height = 720;

    const marrow::renderer::DemoShell shell(window, scene);

    std::cout << shell.launch_report() << '\n';
    std::cout << "Setup-pose region attachment validation passed.\n";
    return 0;
}
