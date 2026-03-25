#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
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

bool require_color(
    const marrow::runtime::SlotColor& color,
    double expected_r,
    double expected_g,
    double expected_b,
    double expected_a,
    std::string_view label) {
    return require_near(color.r, expected_r, std::string(label) + " r") &&
        require_near(color.g, expected_g, std::string(label) + " g") &&
        require_near(color.b, expected_b, std::string(label) + " b") &&
        require_near(color.a, expected_a, std::string(label) + " a");
}

bool require_optional_color(
    const std::optional<marrow::runtime::SlotColor>& color,
    double expected_r,
    double expected_g,
    double expected_b,
    double expected_a,
    std::string_view label) {
    if (!color.has_value()) {
        std::cerr << label << " expected a color but none was provided.\n";
        return false;
    }

    return require_color(*color, expected_r, expected_g, expected_b, expected_a, label);
}

bool require_no_color(
    const std::optional<marrow::runtime::SlotColor>& color,
    std::string_view label) {
    if (!color.has_value()) {
        return true;
    }

    std::cerr << label << " expected no color but one was provided.\n";
    return false;
}

bool validate_skinning_influence(
    const marrow::renderer::GpuSkinningVertexPayload& payload,
    std::size_t influence_index,
    std::size_t expected_bone_index,
    double expected_x,
    double expected_y,
    double expected_weight,
    std::string_view label) {
    if (influence_index >= payload.influence_count) {
        std::cerr << label << " missing expected influence " << influence_index << ".\n";
        return false;
    }

    if (payload.bone_indices[influence_index] != expected_bone_index) {
        std::cerr << label << " expected bone " << expected_bone_index << " but got "
                  << payload.bone_indices[influence_index] << ".\n";
        return false;
    }

    return
        require_near(
            payload.bone_local_positions[influence_index].x,
            expected_x,
            std::string(label) + " x") &&
        require_near(
            payload.bone_local_positions[influence_index].y,
            expected_y,
            std::string(label) + " y") &&
        require_near(
            payload.bone_weights[influence_index],
            expected_weight,
            std::string(label) + " weight");
}

bool validate_skinned_vertex(
    const marrow::renderer::SkinnedMeshVertex& vertex,
    double expected_x,
    double expected_y,
    double expected_u,
    double expected_v,
    std::string_view label) {
    return require_near(vertex.position.x, expected_x, std::string(label) + " x") &&
        require_near(vertex.position.y, expected_y, std::string(label) + " y") &&
        require_near(vertex.uv.x, expected_u, std::string(label) + " u") &&
        require_near(vertex.uv.y, expected_v, std::string(label) + " v");
}

const marrow::renderer::RegionAttachmentDrawCommand* find_region_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& attachment : scene.region_attachments) {
        if (attachment.slot_name == slot_name) {
            return &attachment;
        }
    }

    return nullptr;
}

const marrow::renderer::DynamicMeshDrawCommand* find_dynamic_mesh_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& attachment : scene.dynamic_mesh_attachments) {
        if (attachment.slot_name == slot_name) {
            return &attachment;
        }
    }

    return nullptr;
}

const marrow::renderer::ClipAttachmentDrawCommand* find_clip_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& attachment : scene.clip_attachments) {
        if (attachment.slot_name == slot_name) {
            return &attachment;
        }
    }

    return nullptr;
}

bool require_masked_region_bounds(
    const marrow::renderer::RegionAttachmentDrawCommand& attachment,
    double expected_min_x,
    double expected_min_y,
    double expected_max_x,
    double expected_max_y,
    std::string_view label) {
    if (attachment.masked_vertices.empty()) {
        std::cerr << label << " expected masked vertices but none were generated.\n";
        return false;
    }

    double min_x = attachment.masked_vertices.front().position.x;
    double min_y = attachment.masked_vertices.front().position.y;
    double max_x = min_x;
    double max_y = min_y;
    for (const auto& vertex : attachment.masked_vertices) {
        min_x = std::min(min_x, vertex.position.x);
        min_y = std::min(min_y, vertex.position.y);
        max_x = std::max(max_x, vertex.position.x);
        max_y = std::max(max_y, vertex.position.y);
    }

    return require_near(min_x, expected_min_x, std::string(label) + " min x") &&
        require_near(min_y, expected_min_y, std::string(label) + " min y") &&
        require_near(max_x, expected_max_x, std::string(label) + " max x") &&
        require_near(max_y, expected_max_y, std::string(label) + " max y");
}

bool require_mask_matches_clip(
    const marrow::renderer::RegionAttachmentDrawCommand& attachment,
    const marrow::renderer::ClipAttachmentDrawCommand& clip_attachment,
    std::string_view label) {
    if (clip_attachment.polygon.empty()) {
        std::cerr << label << " expected a clip polygon but none was provided.\n";
        return false;
    }

    double min_x = clip_attachment.polygon.front().x;
    double min_y = clip_attachment.polygon.front().y;
    double max_x = min_x;
    double max_y = min_y;
    for (const auto& vertex : clip_attachment.polygon) {
        min_x = std::min(min_x, vertex.x);
        min_y = std::min(min_y, vertex.y);
        max_x = std::max(max_x, vertex.x);
        max_y = std::max(max_y, vertex.y);
    }

    return require_masked_region_bounds(attachment, min_x, min_y, max_x, max_y, label);
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

    const auto body_slot_index = skeleton_result.skeleton_data->find_slot_index("body");
    const auto spine_index = skeleton_result.skeleton_data->find_bone_index("spine");
    const auto arm_index = skeleton_result.skeleton_data->find_bone_index("arm_l");
    if (!body_slot_index.has_value() || !spine_index.has_value() || !arm_index.has_value()) {
        std::cerr << "Fixture did not preserve the indices needed for renderer mesh validation.\n";
        return 1;
    }
    // Sample after the slot swap so the renderer has to honor linked-mesh deform offsets
    // and weighted GPU skinning in the same draw command.
    constexpr double kAnimatedSampleTime = 0.75;

    marrow::runtime::Skeleton skeleton(skeleton_result.skeleton_data);
    skeleton.set_to_setup_pose();

    const auto scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return 1;
    }

    const marrow::renderer::PreparedScene& scene = *scene_result.scene;
    if (scene.clip_attachments.size() != 1 ||
        scene.region_attachments.size() != 3 ||
        !scene.dynamic_mesh_attachments.empty()) {
        std::cerr << "Expected 1 setup-pose clip attachment, 3 region attachments, and 0 dynamic meshes"
                  << " but prepared clips=" << scene.clip_attachments.size()
                  << ", regions=" << scene.region_attachments.size()
                  << ", meshes=" << scene.dynamic_mesh_attachments.size() << ".\n";
        return 1;
    }

    const auto* setup_body = find_region_attachment(scene, "body");
    const auto* setup_arm = find_region_attachment(scene, "arm_l");
    const auto* setup_spark = find_region_attachment(scene, "spark_fx");
    const auto* setup_clip = find_clip_attachment(scene, "fx_mask");
    if (setup_body == nullptr || setup_arm == nullptr ||
        setup_spark == nullptr || setup_clip == nullptr) {
        std::cerr << "Setup-pose scene did not preserve the expected body, arm, spark, and clip draw commands.\n";
        return 1;
    }

    const bool body_ok = validate_attachment(
        *setup_body,
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
        *setup_arm,
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
    const bool spark_ok = validate_attachment(
        *setup_spark,
        "spark_fx",
        "spark_fx",
        "spark_fx_0",
        1,
        -16.0,
        34.0,
        16.0,
        66.0,
        0.5,
        0.75,
        0.625,
        0.875);
    if (!body_ok || !arm_ok || !spark_ok) {
        std::cerr << "Setup-pose region attachment placement did not match the fixture transforms.\n";
        return 1;
    }
    if (setup_body->blend_mode != marrow::runtime::BlendMode::Screen ||
        !require_color(
            setup_body->color,
            1.0,
            0.8,
            0.6,
            1.0,
            "setup body light color") ||
        !require_optional_color(
            setup_body->dark_color,
            0.2,
            0.4,
            0.6,
            1.0,
            "setup body dark color") ||
        setup_arm->blend_mode != marrow::runtime::BlendMode::Normal ||
        !require_color(
            setup_arm->color,
            1.0,
            1.0,
            1.0,
            1.0,
            "setup arm light color") ||
        !require_no_color(setup_arm->dark_color, "setup arm dark color") ||
        setup_spark->clip_attachment_name != std::optional<std::string>{"fx_mask"} ||
        !require_mask_matches_clip(*setup_spark, *setup_clip, "setup spark clip") ||
        setup_spark->masked_indices.size() != 6 ||
        setup_clip->end_slot_name != "spark_fx" ||
        setup_clip->polygon.size() != 4 ||
        !require_near(setup_clip->polygon[0].x, -6.0, "clip polygon min x") ||
        !require_near(setup_clip->polygon[0].y, 40.0, "clip polygon min y") ||
        !require_near(setup_clip->polygon[2].x, 14.0, "clip polygon max x") ||
        !require_near(setup_clip->polygon[2].y, 60.0, "clip polygon max y")) {
        std::cerr << "Setup-pose slot blend mode or two-color tint did not propagate.\n";
        return 1;
    }

    marrow::renderer::SampleAppWindow window;
    window.title = "Marrow Render Validation";
    window.width = 1280;
    window.height = 720;

    const marrow::renderer::DemoShell shell(window, scene);

    std::cout << shell.launch_report() << '\n';
    std::cout << "Setup-pose region attachment validation passed.\n";

    skeleton.advance_attachment_playback(0.375);
    const auto sequence_scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
    if (!sequence_scene_result) {
        std::cerr << sequence_scene_result.error_message << '\n';
        return 1;
    }

    const marrow::renderer::PreparedScene& sequence_scene = *sequence_scene_result.scene;
    const auto* sequence_spark = find_region_attachment(sequence_scene, "spark_fx");
    const auto* sequence_clip = find_clip_attachment(sequence_scene, "fx_mask");
    if (sequence_scene.clip_attachments.size() != 1 ||
        sequence_clip == nullptr ||
        sequence_spark == nullptr ||
        sequence_spark->atlas_region_name != "spark_fx_3" ||
        sequence_spark->clip_attachment_name != std::optional<std::string>{"fx_mask"} ||
        !require_mask_matches_clip(*sequence_spark, *sequence_clip, "sequence spark clip")) {
        std::cerr << "Sequence playback did not advance the clipped spark region through the renderer.\n";
        return 1;
    }

    const marrow::renderer::DemoShell sequence_shell(window, sequence_scene);
    std::cout << sequence_shell.launch_report() << '\n';
    std::cout << "Sequence attachment playback validation passed at t=0.375.\n";
    skeleton.set_attachment_playback_time(0.0);

    const marrow::runtime::AnimationData* idle_animation =
        skeleton_result.skeleton_data->find_animation("idle");
    if (idle_animation == nullptr) {
        std::cerr << "Fixture did not load the idle animation for renderer validation.\n";
        return 1;
    }

    skeleton.apply_animation(*idle_animation, kAnimatedSampleTime);
    const marrow::runtime::AttachmentData* animated_body_attachment =
        skeleton.current_attachment(*body_slot_index);
    const auto animated_scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
    if (!animated_scene_result) {
        std::cerr << animated_scene_result.error_message << '\n';
        return 1;
    }

    const marrow::renderer::PreparedScene& animated_scene = *animated_scene_result.scene;
    if (animated_scene.clip_attachments.size() != 1 ||
        animated_scene.region_attachments.size() != 2 ||
        animated_scene.dynamic_mesh_attachments.size() != 1) {
        std::cerr << "Animated body attachment was "
                  << (animated_body_attachment != nullptr
                          ? animated_body_attachment->name
                          : std::string("<null>"))
                  << " with mesh="
                  << (animated_body_attachment != nullptr &&
                              animated_body_attachment->mesh_geometry != nullptr
                          ? "yes"
                          : "no")
                  << ".\n";
        std::cerr << "Expected 1 animated clip attachment, 2 animated region attachments, and 1 dynamic mesh attachment but prepared "
                  << animated_scene.clip_attachments.size() << " clip attachments, "
                  << animated_scene.region_attachments.size() << " region attachments and "
                  << animated_scene.dynamic_mesh_attachments.size() << " dynamic mesh attachments.\n";
        return 1;
    }

    const auto* draw_back = find_region_attachment(animated_scene, "arm_l");
    const auto* draw_spark = find_region_attachment(animated_scene, "spark_fx");
    const auto* draw_front = find_dynamic_mesh_attachment(animated_scene, "body");
    const auto* animated_clip = find_clip_attachment(animated_scene, "fx_mask");
    if (draw_back == nullptr || draw_spark == nullptr || draw_front == nullptr ||
        animated_clip == nullptr) {
        std::cerr << "Animated scene did not preserve the expected arm, spark, and body draw commands.\n";
        return 1;
    }

    if (draw_back->attachment_name != "arm_l" ||
        draw_back->atlas_region_name != "arm_l" ||
        draw_back->slot_index != 1 ||
        draw_back->bone_index != 2) {
        std::cerr << "Animated draw order did not place the arm slot first.\n";
        return 1;
    }
    if (draw_front->attachment_name != "warrior_body" ||
        draw_front->atlas_region_name != "warrior_body" ||
        draw_front->slot_index != *body_slot_index ||
        draw_front->blend_mode != marrow::runtime::BlendMode::Screen ||
        draw_front->vertex_buffer_usage != marrow::renderer::MeshBufferUsage::Dynamic ||
        draw_front->shader_path != marrow::renderer::MeshShaderPath::GpuSkinning ||
        animated_body_attachment == nullptr ||
        !animated_body_attachment->linked_mesh.has_value() ||
        !animated_body_attachment->linked_mesh->deform) {
        std::cerr << "Animated weighted mesh did not use the GPU skinning draw path.\n";
        return 1;
    }
    if (draw_spark->atlas_region_name != "spark_fx_0" ||
        draw_spark->clip_attachment_name != std::optional<std::string>{"fx_mask"} ||
        draw_spark->masked_vertices.empty() ||
        draw_spark->masked_indices.empty()) {
        std::cerr << "Animated scene did not preserve the clipped spark sequence attachment.\n";
        return 1;
    }
    if (animated_scene.bone_palette.size() != skeleton.bone_world_transforms().size()) {
        std::cerr << "Prepared scene did not upload the current skeleton bone palette.\n";
        return 1;
    }
    if (draw_back->blend_mode != marrow::runtime::BlendMode::Normal ||
        !require_color(draw_back->color, 1.0, 1.0, 1.0, 1.0, "arm slot color") ||
        !require_no_color(draw_back->dark_color, "arm slot dark color") ||
        !require_color(draw_front->color, 0.6, 0.8, 1.0, 0.5, "body slot color") ||
        !require_optional_color(
            draw_front->dark_color,
            0.2,
            0.4,
            0.6,
            1.0,
            "body slot dark color")) {
        std::cerr << "Animated slot blend mode or two-color tint did not propagate into renderer draw commands.\n";
        return 1;
    }
    if (draw_front->vertex_payloads.size() != 4 || draw_front->indices.size() != 6) {
        std::cerr << "Animated weighted mesh did not upload the expected dynamic mesh buffers.\n";
        return 1;
    }

    const auto& top_left = draw_front->vertex_payloads[0];
    const auto& top_right = draw_front->vertex_payloads[1];
    const auto& bottom_right = draw_front->vertex_payloads[2];
    if (top_left.influence_count != 1 || top_right.influence_count != 2 ||
        bottom_right.influence_count != 2 ||
        !validate_skinning_influence(
            top_left,
            0,
            *spine_index,
            -64.0,
            -80.0,
            1.0,
            "top-left upload") ||
        !validate_skinning_influence(
            top_right,
            0,
            *spine_index,
            70.0,
            -84.0,
            0.75,
            "top-right upload spine") ||
        !validate_skinning_influence(
            top_right,
            1,
            *arm_index,
            100.0,
            -94.0,
            0.25,
            "top-right upload arm") ||
        !validate_skinning_influence(
            bottom_right,
            0,
            *spine_index,
            72.0,
            90.0,
            0.25,
            "bottom-right upload spine") ||
        !validate_skinning_influence(
            bottom_right,
            1,
            *arm_index,
            102.0,
            80.0,
            0.75,
            "bottom-right upload arm")) {
        std::cerr << "Renderer did not upload the authored weighted mesh influences.\n";
        return 1;
    }

    const std::vector<marrow::renderer::SkinnedMeshVertex> skinned_vertices =
        marrow::renderer::evaluate_gpu_skinned_vertices(
            *draw_front,
            animated_scene.bone_palette);
    const std::optional<marrow::runtime::MeshAttachmentPose> runtime_mesh =
        skeleton.evaluate_current_mesh_attachment(*body_slot_index);
    if (!runtime_mesh.has_value() || skinned_vertices.size() != runtime_mesh->vertices.size()) {
        std::cerr << "GPU skinning validation could not compare against the runtime mesh pose.\n";
        return 1;
    }
    if (!validate_skinned_vertex(
            skinned_vertices[0],
            runtime_mesh->vertices[0].x,
            runtime_mesh->vertices[0].y,
            0.0,
            0.625,
            "skinned vertex 0") ||
        !validate_skinned_vertex(
            skinned_vertices[1],
            runtime_mesh->vertices[1].x,
            runtime_mesh->vertices[1].y,
            0.5,
            0.625,
            "skinned vertex 1") ||
        !validate_skinned_vertex(
            skinned_vertices[2],
            runtime_mesh->vertices[2].x,
            runtime_mesh->vertices[2].y,
            0.5,
            1.0,
            "skinned vertex 2") ||
        !validate_skinned_vertex(
            skinned_vertices[3],
            runtime_mesh->vertices[3].x,
            runtime_mesh->vertices[3].y,
            0.0,
            1.0,
            "skinned vertex 3")) {
        std::cerr << "GPU skinning vertex evaluation did not match the runtime weighted mesh pose.\n";
        return 1;
    }

    const marrow::renderer::DemoShell animated_shell(window, animated_scene);
    std::cout << animated_shell.launch_report() << '\n';
    std::cout << "Animated slot timeline presentation validation passed.\n";
    std::cout << "GPU-skinned weighted mesh validation passed.\n";
    std::cout << "GPU-skinned FFD deform timeline validation passed at t="
              << kAnimatedSampleTime << ".\n";
    return 0;
}
