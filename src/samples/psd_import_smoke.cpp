#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "marrow/editor/psd_import.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/json.hpp"

namespace {

struct Options {
    std::filesystem::path initial_psd{"assets/fixtures/psd_import_sample.psd"};
    std::filesystem::path reimport_psd{"assets/fixtures/psd_import_sample_reimport.psd"};
};

bool expect(bool condition, std::string_view message) {
    if (condition) {
        return true;
    }
    std::cerr << message << '\n';
    return false;
}

bool expect_near(double actual, double expected, double epsilon, std::string_view label) {
    if (std::abs(actual - expected) <= epsilon) {
        return true;
    }
    std::cerr << label << " expected " << expected << " but was " << actual << '\n';
    return false;
}

marrow::runtime::json::Value make_number_value(double value) {
    return marrow::runtime::json::Value(value, {});
}

marrow::runtime::json::Value make_string_value(std::string value) {
    return marrow::runtime::json::Value(std::move(value), {});
}

marrow::runtime::json::Value make_array_value(marrow::runtime::json::Value::Array values = {}) {
    return marrow::runtime::json::Value(std::move(values), {});
}

marrow::runtime::json::Value make_object_value(marrow::runtime::json::Value::Object values = {}) {
    return marrow::runtime::json::Value(std::move(values), {});
}

bool write_text_file(const std::filesystem::path& path, std::string_view text) {
    const std::filesystem::path parent_path = path.parent_path();
    if (!parent_path.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent_path, error);
        if (error) {
            std::cerr << error.message() << '\n';
            return false;
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "failed to open " << path.string() << '\n';
        return false;
    }
    output << text;
    if (!output.good()) {
        std::cerr << "failed to write " << path.string() << '\n';
        return false;
    }
    return true;
}

bool patch_animation_for_reimport(const std::filesystem::path& skeleton_path) {
    const auto load_result = marrow::runtime::json::load_document(skeleton_path);
    if (!load_result) {
        std::cerr << load_result.error->format() << '\n';
        return false;
    }

    marrow::runtime::json::Document document = *load_result.document;
    if (!document.root.is_object()) {
        std::cerr << "expected an object root when patching the imported skeleton.\n";
        return false;
    }

    auto& root = document.root.as_object();
    marrow::runtime::json::Value::Object animation_root;
    marrow::runtime::json::Value::Object torso_bone;
    torso_bone.emplace(
        "rotate",
        make_array_value({
            make_object_value({
                {"time", make_number_value(0.0)},
                {"angle", make_number_value(0.0)},
                {"curve", make_string_value("linear")},
            }),
            make_object_value({
                {"time", make_number_value(0.25)},
                {"angle", make_number_value(12.0)},
                {"curve", make_string_value("linear")},
            }),
        }));

    marrow::runtime::json::Value::Object bones;
    bones.emplace("torso", make_object_value(std::move(torso_bone)));
    animation_root.emplace("bones", make_object_value(std::move(bones)));
    root["animations"] = make_object_value({
        {"idle", make_object_value(std::move(animation_root))},
    });

    return write_text_file(
        skeleton_path,
        marrow::runtime::json::serialize_pretty(document.root));
}

std::optional<marrow::renderer::RegionAttachmentDrawCommand> find_region_draw_command(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& draw_command : scene.draw_commands) {
        if (const auto* region =
                std::get_if<marrow::renderer::RegionAttachmentDrawCommand>(&draw_command);
            region != nullptr && region->slot_name == slot_name) {
            return *region;
        }
    }
    return std::nullopt;
}

std::pair<marrow::renderer::RenderPoint, marrow::renderer::RenderPoint> command_bounds(
    const marrow::renderer::RegionAttachmentDrawCommand& command) {
    marrow::renderer::RenderPoint min_corner = command.vertices.front().position;
    marrow::renderer::RenderPoint max_corner = command.vertices.front().position;
    for (const auto& vertex : command.vertices) {
        min_corner.x = std::min(min_corner.x, vertex.position.x);
        min_corner.y = std::min(min_corner.y, vertex.position.y);
        max_corner.x = std::max(max_corner.x, vertex.position.x);
        max_corner.y = std::max(max_corner.y, vertex.position.y);
    }
    return {min_corner, max_corner};
}

bool validate_initial_import(
    const marrow::editor::PsdImportResult& import_result,
    const std::filesystem::path& skeleton_path,
    const std::filesystem::path& atlas_path) {
    if (!expect(import_result.layers.size() == 3U, "expected three imported PSD layers")) {
        return false;
    }
    if (!expect(import_result.bones.size() == 2U, "expected root plus one folder bone")) {
        return false;
    }
    if (!expect(
            std::filesystem::exists(import_result.extracted_layers_directory / "body.png") ||
                std::filesystem::exists(import_result.extracted_layers_directory / "body_2.png"),
            "expected extracted layer PNGs to be written")) {
        return false;
    }

    const auto skeleton_result = marrow::runtime::load_skeleton_data(skeleton_path);
    if (!skeleton_result) {
        std::cerr << skeleton_result.error->format() << '\n';
        return false;
    }
    const auto atlas_result = marrow::runtime::AtlasLoader::load(atlas_path);
    if (!atlas_result) {
        std::cerr << atlas_result.error->format() << '\n';
        return false;
    }

    const auto torso_index = std::find_if(
        skeleton_result.skeleton_data->bones().begin(),
        skeleton_result.skeleton_data->bones().end(),
        [](const marrow::runtime::BoneData& bone) {
            return bone.name == "torso";
        });
    if (!expect(
            torso_index != skeleton_result.skeleton_data->bones().end(),
            "expected the PSD folder to become a torso bone")) {
        return false;
    }

    const auto body_slot = skeleton_result.skeleton_data->find_slot_index("body");
    const auto arm_slot = skeleton_result.skeleton_data->find_slot_index("arm_l");
    const auto shadow_slot = skeleton_result.skeleton_data->find_slot_index("shadow");
    if (!expect(
            body_slot.has_value() && arm_slot.has_value() && shadow_slot.has_value(),
            "expected body, arm_l, and shadow slots")) {
        return false;
    }

    const auto& slots = skeleton_result.skeleton_data->slots();
    if (!expect(
            skeleton_result.skeleton_data->bones()[slots[*body_slot].bone_index].name == "torso" &&
                skeleton_result.skeleton_data->bones()[slots[*arm_slot].bone_index].name == "torso" &&
                skeleton_result.skeleton_data->bones()[slots[*shadow_slot].bone_index].name == "root",
            "layer-to-bone mapping did not preserve the folder hierarchy")) {
        return false;
    }

    const auto* body_region = atlas_result.atlas_data->find_region("body");
    const auto* arm_region = atlas_result.atlas_data->find_region("arm_l");
    const auto* shadow_region = atlas_result.atlas_data->find_region("shadow");
    if (!expect(
            body_region != nullptr && arm_region != nullptr && shadow_region != nullptr,
            "expected atlas regions for each imported layer")) {
        return false;
    }
    if (!expect_near(body_region->origin_x, -12.0, 1e-6, "body origin_x") ||
        !expect_near(body_region->origin_y, 0.0, 1e-6, "body origin_y") ||
        !expect_near(arm_region->origin_x, 0.0, 1e-6, "arm_l origin_x") ||
        !expect_near(arm_region->origin_y, -8.0, 1e-6, "arm_l origin_y")) {
        return false;
    }

    marrow::runtime::Skeleton preview(skeleton_result.skeleton_data);
    preview.set_to_setup_pose();
    preview.update_world_transforms();
    const auto scene_result =
        marrow::renderer::prepare_setup_pose_scene(preview, *atlas_result.atlas_data);
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return false;
    }

    const auto body_draw = find_region_draw_command(*scene_result.scene, "body");
    const auto arm_draw = find_region_draw_command(*scene_result.scene, "arm_l");
    const auto shadow_draw = find_region_draw_command(*scene_result.scene, "shadow");
    if (!expect(
            body_draw.has_value() && arm_draw.has_value() && shadow_draw.has_value(),
            "renderer scene did not include each imported layer")) {
        return false;
    }

    const auto [body_min, body_max] = command_bounds(*body_draw);
    const auto [arm_min, arm_max] = command_bounds(*arm_draw);
    const auto [shadow_min, shadow_max] = command_bounds(*shadow_draw);
    if (!expect_near(body_min.x, 16.0, 1e-6, "body min x") ||
        !expect_near(body_min.y, 12.0, 1e-6, "body min y") ||
        !expect_near(body_max.x, 36.0, 1e-6, "body max x") ||
        !expect_near(body_max.y, 36.0, 1e-6, "body max y") ||
        !expect_near(arm_min.x, 4.0, 1e-6, "arm_l min x") ||
        !expect_near(arm_min.y, 20.0, 1e-6, "arm_l min y") ||
        !expect_near(arm_max.x, 16.0, 1e-6, "arm_l max x") ||
        !expect_near(arm_max.y, 28.0, 1e-6, "arm_l max y") ||
        !expect_near(shadow_min.x, 18.0, 1e-6, "shadow min x") ||
        !expect_near(shadow_min.y, 40.0, 1e-6, "shadow min y") ||
        !expect_near(shadow_max.x, 32.0, 1e-6, "shadow max x") ||
        !expect_near(shadow_max.y, 48.0, 1e-6, "shadow max y")) {
        return false;
    }

    return true;
}

bool validate_reimport(
    const std::filesystem::path& skeleton_path,
    const std::filesystem::path& atlas_path) {
    const auto document_result = marrow::runtime::json::load_document(skeleton_path);
    if (!document_result) {
        std::cerr << document_result.error->format() << '\n';
        return false;
    }

    const auto* animations = marrow::runtime::json::find_member(
        document_result.document->root,
        "animations");
    if (!expect(
            animations != nullptr && animations->is_object() &&
                marrow::runtime::json::find_member(*animations, "idle") != nullptr,
            "re-import did not preserve the existing animation data")) {
        return false;
    }

    const auto skeleton_result = marrow::runtime::load_skeleton_data(skeleton_path);
    if (!skeleton_result) {
        std::cerr << skeleton_result.error->format() << '\n';
        return false;
    }
    const auto atlas_result = marrow::runtime::AtlasLoader::load(atlas_path);
    if (!atlas_result) {
        std::cerr << atlas_result.error->format() << '\n';
        return false;
    }

    marrow::runtime::Skeleton preview(skeleton_result.skeleton_data);
    preview.set_to_setup_pose();
    preview.update_world_transforms();
    const auto scene_result =
        marrow::renderer::prepare_setup_pose_scene(preview, *atlas_result.atlas_data);
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return false;
    }

    const auto body_draw = find_region_draw_command(*scene_result.scene, "body");
    if (!expect(body_draw.has_value(), "re-import scene was missing the body layer")) {
        return false;
    }
    const auto [body_min, body_max] = command_bounds(*body_draw);
    if (!expect_near(body_min.x, 20.0, 1e-6, "reimported body min x") ||
        !expect_near(body_min.y, 14.0, 1e-6, "reimported body min y") ||
        !expect_near(body_max.x, 40.0, 1e-6, "reimported body max x") ||
        !expect_near(body_max.y, 38.0, 1e-6, "reimported body max y")) {
        return false;
    }

    return true;
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (argc == 3) {
        options.initial_psd = argv[1];
        options.reimport_psd = argv[2];
    } else if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << " [initial.psd reimport.psd]\n";
        return 1;
    }

    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_psd_import_smoke";
    std::error_code error;
    std::filesystem::remove_all(temp_root, error);
    error.clear();
    std::filesystem::create_directories(temp_root, error);
    if (error) {
        std::cerr << error.message() << '\n';
        return 1;
    }

    const std::filesystem::path skeleton_path = temp_root / "psd_import_output.mskl";
    const std::filesystem::path atlas_path = temp_root / "psd_import_output.matl";
    marrow::editor::PsdImportOptions import_options;
    import_options.psd_path = options.initial_psd;
    import_options.skeleton_output_path = skeleton_path;
    import_options.atlas_output_path = atlas_path;

    const auto import_result = marrow::editor::import_psd_to_runtime_bundle(import_options);
    if (!import_result) {
        std::cerr << import_result.error->format() << '\n';
        return 1;
    }
    if (!validate_initial_import(import_result, skeleton_path, atlas_path)) {
        return 1;
    }

    if (!patch_animation_for_reimport(skeleton_path)) {
        return 1;
    }

    import_options.psd_path = options.reimport_psd;
    import_options.existing_skeleton_path = skeleton_path;
    const auto reimport_result = marrow::editor::import_psd_to_runtime_bundle(import_options);
    if (!reimport_result) {
        std::cerr << reimport_result.error->format() << '\n';
        return 1;
    }
    if (!validate_reimport(skeleton_path, atlas_path)) {
        return 1;
    }

    std::cout << "Imported PSD layers into "
              << skeleton_path.string()
              << " and "
              << atlas_path.string()
              << '\n';
    std::cout << "Extracted layer images: "
              << reimport_result.layers.size()
              << '\n';
    std::cout << "Re-import preserved authored animation data.\n";
    return 0;
}
