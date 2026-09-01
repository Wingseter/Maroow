#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#include "atlas_packer.hpp"
#include "marrow/editor/psd_reimport_plan.hpp"
#include "marrow/editor/psd_import.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/json.hpp"

namespace {

struct Options {
    std::filesystem::path initial_psd{"assets/fixtures/psd_import_sample.psd"};
    std::filesystem::path reimport_psd{"assets/fixtures/psd_import_sample_reimport.psd"};
};

constexpr double kRenderBoundsEpsilon = 1e-5;

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
    std::cerr << label << " expected " << std::setprecision(17) << expected
              << " but was " << actual << '\n';
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
    if (!expect_near(body_min.x, 16.0, kRenderBoundsEpsilon, "body min x") ||
        !expect_near(body_min.y, 12.0, kRenderBoundsEpsilon, "body min y") ||
        !expect_near(body_max.x, 36.0, kRenderBoundsEpsilon, "body max x") ||
        !expect_near(body_max.y, 36.0, kRenderBoundsEpsilon, "body max y") ||
        !expect_near(arm_min.x, 4.0, kRenderBoundsEpsilon, "arm_l min x") ||
        !expect_near(arm_min.y, 20.0, kRenderBoundsEpsilon, "arm_l min y") ||
        !expect_near(arm_max.x, 16.0, kRenderBoundsEpsilon, "arm_l max x") ||
        !expect_near(arm_max.y, 28.0, kRenderBoundsEpsilon, "arm_l max y") ||
        !expect_near(shadow_min.x, 18.0, kRenderBoundsEpsilon, "shadow min x") ||
        !expect_near(shadow_min.y, 40.0, kRenderBoundsEpsilon, "shadow min y") ||
        !expect_near(shadow_max.x, 32.0, kRenderBoundsEpsilon, "shadow max x") ||
        !expect_near(shadow_max.y, 48.0, kRenderBoundsEpsilon, "shadow max y")) {
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
    if (!expect_near(body_min.x, 20.0, kRenderBoundsEpsilon, "reimported body min x") ||
        !expect_near(body_min.y, 14.0, kRenderBoundsEpsilon, "reimported body min y") ||
        !expect_near(body_max.x, 40.0, kRenderBoundsEpsilon, "reimported body max x") ||
        !expect_near(body_max.y, 38.0, kRenderBoundsEpsilon, "reimported body max y")) {
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// MAR-188: a PSD synthesiser, and the gate that makes its output trustworthy.
//
// The two checked-in fixtures differ in FOUR bytes and have identical layer sets:
// no added, removed or renamed layer, no group move, no duplicate name, and no
// group nested deeper than one level exists anywhere in the repository. Every
// classification case this story needs therefore has no input, and hand-authoring
// more opaque ~19KB binaries would leave a reviewer unable to see what a fixture
// contains without running something.
//
// So the inputs are synthesised from a declarative layer list, in the test, beside
// the assertions that read them. Raw compression only: the PackBits decoder is the
// parser's problem, and a writer that also implements RLE is a second thing to
// debug.
//
// The obvious risk is that a writer built by reading the parser encodes the
// parser's own assumptions and proves nothing. Q0 is the mitigation, and it is a
// gate rather than a hope -- see `validate_q0_synthesiser_gate`.
// ---------------------------------------------------------------------------

namespace mar188 {

/** @brief One layer to synthesise. Groups are implied by `group_path`. */
struct SynthLayer {
    std::vector<std::string> group_path;
    std::string name;
    int left{0};
    int top{0};
    int width{0};
    int height{0};
    std::uint8_t red{0};
    std::uint8_t green{0};
    std::uint8_t blue{0};
};

void push_u16(std::vector<std::uint8_t>* out, std::uint16_t value) {
    out->push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
    out->push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void push_u32(std::vector<std::uint8_t>* out, std::uint32_t value) {
    out->push_back(static_cast<std::uint8_t>((value >> 24) & 0xFFU));
    out->push_back(static_cast<std::uint8_t>((value >> 16) & 0xFFU));
    out->push_back(static_cast<std::uint8_t>((value >> 8) & 0xFFU));
    out->push_back(static_cast<std::uint8_t>(value & 0xFFU));
}

void push_i32(std::vector<std::uint8_t>* out, int value) {
    push_u32(out, static_cast<std::uint32_t>(static_cast<std::int32_t>(value)));
}

void push_signature(std::vector<std::uint8_t>* out, const char* text) {
    for (std::size_t index = 0; index < 4U; ++index) {
        out->push_back(static_cast<std::uint8_t>(text[index]));
    }
}

/** @brief Pascal string: length byte, bytes, then NUL padding to a multiple of 4. */
void push_pascal_string(std::vector<std::uint8_t>* out, const std::string& value) {
    const std::size_t length = std::min<std::size_t>(value.size(), 255U);
    out->push_back(static_cast<std::uint8_t>(length));
    for (std::size_t index = 0; index < length; ++index) {
        out->push_back(static_cast<std::uint8_t>(value[index]));
    }
    const std::size_t padded = ((1U + length + 3U) / 4U) * 4U;
    for (std::size_t index = 1U + length; index < padded; ++index) {
        out->push_back(0U);
    }
}

/**
 * @brief One layer record header. `section_type` 0 means an ordinary pixel layer.
 *
 * A record with channels declares them here and its pixel bytes are appended to a
 * SEPARATE stream that follows every record -- that split is what the layout
 * requires and what makes the sizes non-obvious.
 */
void push_layer_record(
    std::vector<std::uint8_t>* out,
    const std::string& name,
    int left,
    int top,
    int right,
    int bottom,
    std::uint16_t channel_count,
    std::uint32_t channel_data_length,
    std::uint32_t section_type) {
    push_i32(out, top);
    push_i32(out, left);
    push_i32(out, bottom);
    push_i32(out, right);

    push_u16(out, channel_count);
    // Ids are red 0, green 1, blue 2 and alpha -1 -- the four the assembler maps,
    // and the four the checked-in fixture uses. Q0's header clause caught a
    // three-channel writer whose LAYER REPORT was already identical.
    static const std::int16_t kChannelIds[4] = {0, 1, 2, -1};
    for (std::uint16_t index = 0; index < channel_count; ++index) {
        push_u16(out, static_cast<std::uint16_t>(kChannelIds[index]));
        push_u32(out, channel_data_length);
    }

    push_signature(out, "8BIM");
    push_signature(out, "norm");
    out->push_back(255U);  // opacity
    out->push_back(0U);    // clipping
    out->push_back(0U);    // flags: bit 1 clear means VISIBLE
    out->push_back(0U);    // filler

    std::vector<std::uint8_t> extra;
    push_u32(&extra, 0U);  // layer mask data length
    push_u32(&extra, 0U);  // blending ranges length
    push_pascal_string(&extra, name);
    if (section_type != 0U) {
        push_signature(&extra, "8BIM");
        push_signature(&extra, "lsct");
        push_u32(&extra, 4U);
        push_u32(&extra, section_type);
    }
    push_u32(out, static_cast<std::uint32_t>(extra.size()));
    out->insert(out->end(), extra.begin(), extra.end());
}

/**
 * @brief Writes an 8BPS v1, RGB, 8-bit, raw-compression PSD.
 *
 * Group ORDER is the load-bearing part: each group's `lsct` 1 header is emitted
 * BEFORE its children and its `lsct` 3 divider AFTER them, which is the order the
 * importer's `active_groups` stack accepts and the order both checked-in fixtures
 * use. Whether a Photoshop-authored file is ordered the same way is NOT settled
 * here and cannot be -- there is no Photoshop-authored PSD in this repository.
 */
bool write_synthetic_psd(
    const std::filesystem::path& path,
    int canvas_width,
    int canvas_height,
    const std::vector<SynthLayer>& layers) {
    std::vector<std::uint8_t> records;
    std::vector<std::uint8_t> pixels;
    std::uint16_t record_count = 0U;

    const auto emit_marker = [&](const std::string& name, std::uint32_t section_type) {
        push_layer_record(&records, name, 0, 0, 0, 0, 0U, 0U, section_type);
        ++record_count;
    };

    std::vector<std::string> open_groups;
    for (const SynthLayer& layer : layers) {
        std::size_t common = 0;
        while (common < open_groups.size() && common < layer.group_path.size() &&
               open_groups[common] == layer.group_path[common]) {
            ++common;
        }
        while (open_groups.size() > common) {
            emit_marker("</Layer group>", 3U);
            open_groups.pop_back();
        }
        while (open_groups.size() < layer.group_path.size()) {
            const std::string& group_name = layer.group_path[open_groups.size()];
            emit_marker(group_name, 1U);
            open_groups.push_back(group_name);
        }

        const std::size_t pixel_count =
            static_cast<std::size_t>(layer.width) * static_cast<std::size_t>(layer.height);
        // Each channel is a raw block: a u16 compression tag then one byte per
        // pixel. The declared length must match exactly or the parser refuses.
        const std::uint32_t channel_data_length =
            static_cast<std::uint32_t>(2U + pixel_count);
        push_layer_record(&records, layer.name, layer.left, layer.top,
                          layer.left + layer.width, layer.top + layer.height, 4U,
                          channel_data_length, 0U);
        ++record_count;

        const std::uint8_t components[4] = {layer.red, layer.green, layer.blue, 255U};
        for (const std::uint8_t component : components) {
            push_u16(&pixels, 0U);  // raw
            pixels.insert(pixels.end(), pixel_count, component);
        }
    }
    while (!open_groups.empty()) {
        emit_marker("</Layer group>", 3U);
        open_groups.pop_back();
    }

    std::vector<std::uint8_t> layer_info;
    push_u16(&layer_info, record_count);
    layer_info.insert(layer_info.end(), records.begin(), records.end());
    layer_info.insert(layer_info.end(), pixels.begin(), pixels.end());

    std::vector<std::uint8_t> bytes;
    push_signature(&bytes, "8BPS");
    push_u16(&bytes, 1U);                                  // version
    for (int index = 0; index < 6; ++index) {              // reserved
        bytes.push_back(0U);
    }
    push_u16(&bytes, 4U);                                  // channel count (RGBA)
    push_u32(&bytes, static_cast<std::uint32_t>(canvas_height));
    push_u32(&bytes, static_cast<std::uint32_t>(canvas_width));
    push_u16(&bytes, 8U);                                  // depth
    push_u16(&bytes, 3U);                                  // RGB
    push_u32(&bytes, 0U);                                  // colour mode data
    push_u32(&bytes, 0U);                                  // image resources

    push_u32(&bytes, static_cast<std::uint32_t>(layer_info.size() + 4U));
    push_u32(&bytes, static_cast<std::uint32_t>(layer_info.size()));
    bytes.insert(bytes.end(), layer_info.begin(), layer_info.end());

    if (!path.parent_path().empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(path.parent_path(), directory_error);
        if (directory_error) {
            return false;
        }
    }
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    return stream.good();
}

/** @brief The tree of `psd_import_sample.psd`, which Q0 reproduces. */
std::vector<SynthLayer> fixture_tree() {
    return {
        {{}, "shadow", 18, 40, 14, 8, 10U, 20U, 30U},
        {{"torso"}, "arm_l", 4, 20, 12, 8, 40U, 50U, 60U},
        {{"torso"}, "body", 16, 12, 20, 24, 70U, 80U, 90U},
    };
}

/** @brief One line per layer, in the report's own order. */
std::vector<std::string> layer_report(const marrow::editor::PsdImportResult& result) {
    std::vector<std::string> rows;
    for (const marrow::editor::PsdImportedLayer& layer : result.layers) {
        std::string groups;
        for (const std::string& segment : layer.group_path) {
            groups += segment;
            groups += ",";
        }
        rows.push_back(
            "name=" + layer.name + " group=[" + groups + "] slot=" + layer.slot_name +
            " att=" + layer.attachment_name + " bone=" + layer.bone_name + " img=" +
            layer.extracted_image_path.filename().generic_string() + " box=(" +
            std::to_string(layer.left) + "," + std::to_string(layer.top) + "," +
            std::to_string(layer.width) + "," + std::to_string(layer.height) + ")");
    }
    return rows;
}

/** @brief One line per bone, in the report's own order. */
std::vector<std::string> bone_report(const marrow::editor::PsdImportResult& result) {
    std::vector<std::string> rows;
    for (const marrow::editor::PsdBoneHint& bone : result.bones) {
        rows.push_back(
            "bone=" + bone.name + " parent=" +
            (bone.parent_name.has_value() ? *bone.parent_name : std::string("<none>")) +
            " x=" + std::to_string(static_cast<int>(bone.x)) + " y=" +
            std::to_string(static_cast<int>(bone.y)));
    }
    return rows;
}

/** @brief The first `count` header bytes of a file. */
bool read_header_bytes(
    const std::filesystem::path& path,
    std::size_t count,
    std::vector<std::uint8_t>* bytes_out) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return false;
    }
    bytes_out->assign(count, 0U);
    stream.read(reinterpret_cast<char*>(bytes_out->data()),
                static_cast<std::streamsize>(count));
    return static_cast<std::size_t>(stream.gcount()) == count;
}

}  // namespace mar188

namespace mar188 {

/** @brief Imports a synthesised PSD into a scratch directory. */
bool import_synthetic(
    const std::filesystem::path& psd_path,
    const std::filesystem::path& scratch,
    std::string_view label,
    marrow::editor::PsdImportResult* result_out) {
    marrow::editor::PsdImportOptions options;
    options.psd_path = psd_path;
    options.skeleton_output_path = scratch / "out.mskl";
    options.atlas_output_path = scratch / "out.matl";
    options.extracted_layers_directory = scratch / "out_layers";
    options.atlas_name = "out";
    *result_out = marrow::editor::import_psd_to_runtime_bundle(options);
    if (!*result_out) {
        std::cerr << label << ": import failed: " << result_out->error->format() << '\n';
        return false;
    }
    return true;
}

bool expect_rows(
    std::string_view label,
    const std::vector<std::string>& actual,
    const std::vector<std::string>& expected) {
    if (actual == expected) {
        return true;
    }
    std::cerr << label << ": report mismatch.\n";
    for (const std::string& row : expected) {
        if (std::find(actual.begin(), actual.end(), row) == actual.end()) {
            std::cerr << "  missing:    " << row << '\n';
        }
    }
    for (const std::string& row : actual) {
        if (std::find(expected.begin(), expected.end(), row) == expected.end()) {
            std::cerr << "  unexpected: " << row << '\n';
        }
    }
    if (actual.size() == expected.size()) {
        for (std::size_t index = 0; index < actual.size(); ++index) {
            if (actual[index] != expected[index]) {
                std::cerr << "  first order difference at index " << index << ": expected "
                          << expected[index] << ", got " << actual[index] << '\n';
                break;
            }
        }
    }
    return false;
}

}  // namespace mar188

/**
 * @brief Q0/Q0b -- the synthesiser gate. Runs FIRST; nothing downstream is trusted
 *        until it passes.
 *
 * Four clauses, because the import REPORT is a lossy projection of the file: it
 * carries no pixel data, no channel count, no depth and no colour mode. A writer
 * with a wrong pixel encoding would reproduce the whole report while every
 * extracted PNG was wrong, which would silently corrupt the classification cases
 * and everything a later story stages on top.
 */
bool validate_mar188_q0(
    const std::filesystem::path& reference_psd,
    const std::filesystem::path& scratch) {
    std::error_code directory_error;
    std::filesystem::remove_all(scratch, directory_error);
    std::filesystem::create_directories(scratch, directory_error);

    // -- Clause 1: the report, against the checked-in fixture's OWN output. ---
    // Measured from the fixture at run time rather than hard-coded, so the gate
    // cannot drift away from the file it claims to reproduce.
    marrow::editor::PsdImportResult reference;
    if (!mar188::import_synthetic(reference_psd, scratch / "reference", "Q0 (fixture)",
                                  &reference)) {
        return false;
    }

    const std::filesystem::path synthetic = scratch / "synthetic.psd";
    if (!mar188::write_synthetic_psd(synthetic, 64, 64, mar188::fixture_tree())) {
        std::cerr << "Q0: the synthesiser could not write " << synthetic.generic_string()
                  << ".\n";
        return false;
    }
    marrow::editor::PsdImportResult synthesised;
    if (!mar188::import_synthetic(synthetic, scratch / "synthetic_out", "Q0",
                                  &synthesised)) {
        return false;
    }

    if (!mar188::expect_rows("Q0 (layer report)", mar188::layer_report(synthesised),
                             mar188::layer_report(reference))) {
        return false;
    }
    if (!mar188::expect_rows("Q0 (bone report)", mar188::bone_report(synthesised),
                             mar188::bone_report(reference))) {
        return false;
    }

    // -- Clause 2: pixels. Nothing in the report can see these. ---------------
    // The importer drops a fully transparent layer outright, so an all-zero
    // writer fails loudly; this clause exists for the PARTIALLY wrong encoding --
    // swapped channel order, wrong row padding -- that survives the report.
    {
        const marrow::editor::PsdImportedLayer* body = nullptr;
        for (const marrow::editor::PsdImportedLayer& layer : synthesised.layers) {
            if (layer.name == "body") {
                body = &layer;
            }
        }
        if (body == nullptr) {
            std::cerr << "Q0: the synthesised import produced no 'body' layer.\n";
            return false;
        }
        std::vector<std::uint8_t> png;
        std::ifstream stream(body->extracted_image_path, std::ios::binary);
        if (!stream) {
            std::cerr << "Q0: the extracted image " << body->extracted_image_path.generic_string()
                      << " was not written.\n";
            return false;
        }
        png.assign(std::istreambuf_iterator<char>(stream),
                   std::istreambuf_iterator<char>());
        if (png.size() < 8U || png[0] != 0x89U || png[1] != 'P' || png[2] != 'N' ||
            png[3] != 'G') {
            std::cerr << "Q0: the extracted image is not a PNG (" << png.size()
                      << " bytes).\n";
            return false;
        }
        // The synthesiser fills `body` with a solid colour, so re-encoding that
        // exact RGBA through the importer's OWN writer must reproduce the
        // extracted file byte for byte. Comparing files rather than decoding one
        // keeps the gate free of a second image codec to debug -- the objection
        // that ruled out teaching the synthesiser PackBits applies here too.
        const std::size_t pixel_count =
            static_cast<std::size_t>(body->width) * static_cast<std::size_t>(body->height);
        std::vector<std::uint8_t> expected_rgba(pixel_count * 4U, 0U);
        for (std::size_t index = 0; index < pixel_count; ++index) {
            expected_rgba[(index * 4U) + 0U] = 70U;
            expected_rgba[(index * 4U) + 1U] = 80U;
            expected_rgba[(index * 4U) + 2U] = 90U;
            expected_rgba[(index * 4U) + 3U] = 255U;
        }
        const std::filesystem::path expected_png = scratch / "expected_body.png";
        if (const auto error = marrow::editor::detail::write_rgba_png(
                expected_png, body->width, body->height, expected_rgba)) {
            std::cerr << "Q0: could not write the expected image: " << *error << '\n';
            return false;
        }
        std::ifstream expected_stream(expected_png, std::ios::binary);
        std::vector<std::uint8_t> expected_bytes(
            (std::istreambuf_iterator<char>(expected_stream)),
            std::istreambuf_iterator<char>());
        if (expected_bytes != png) {
            std::cerr << "Q0: the extracted layer image does not match the colour the "
                         "synthesiser wrote. Re-encoding RGBA(70,80,90,255) at "
                      << body->width << "x" << body->height << " gives "
                      << expected_bytes.size() << " bytes; the importer extracted "
                      << png.size()
                      << ". A wrong channel order or row padding survives the layer "
                         "report and is only visible here.\n";
            return false;
        }
    }

    // -- Clause 3: the header, against the fixture's own bytes. ---------------
    {
        std::vector<std::uint8_t> reference_header;
        std::vector<std::uint8_t> synthetic_header;
        if (!mar188::read_header_bytes(reference_psd, 26U, &reference_header) ||
            !mar188::read_header_bytes(synthetic, 26U, &synthetic_header)) {
            std::cerr << "Q0: could not read both PSD headers.\n";
            return false;
        }
        // Signature, version, channel count, depth and colour mode. The canvas
        // size (bytes 14-21) is deliberately excluded: the synthesiser is not
        // reproducing the fixture's canvas, only its layer tree.
        const std::vector<std::pair<std::size_t, std::size_t>> compared = {
            {0U, 6U},    // "8BPS" + version
            {12U, 2U},   // channel count
            {22U, 4U},   // depth + colour mode
        };
        for (const auto& range : compared) {
            for (std::size_t index = range.first; index < range.first + range.second;
                 ++index) {
                if (reference_header[index] != synthetic_header[index]) {
                    std::cerr << "Q0: PSD header byte " << index
                              << " differs from the checked-in fixture (expected "
                              << static_cast<int>(reference_header[index]) << ", got "
                              << static_cast<int>(synthetic_header[index]) << ").\n";
                    return false;
                }
            }
        }
        std::cout << "Q0: synthesised " << std::filesystem::file_size(synthetic)
                  << " bytes against the fixture's "
                  << std::filesystem::file_size(reference_psd)
                  << "; headers agree on signature, version, channels, depth and "
                     "colour mode.\n";
    }

    // -- Clause 4 (Q0b): the duplicate-name branch, which Q0 cannot reach. ----
    // The importer picks the bare layer name when the document-global census is 1
    // and the joined group path when it is greater. Q0's document has no
    // duplicates, so that second branch is UNGATED -- and the escaping and
    // duplicate-refusal cases are the only ones that live in it.
    {
        const std::vector<mar188::SynthLayer> duplicates = {
            {{}, "body", 0, 0, 4, 4, 1U, 2U, 3U},
            {{"torso"}, "body", 8, 8, 4, 4, 4U, 5U, 6U},
        };
        const std::filesystem::path path = scratch / "duplicates.psd";
        if (!mar188::write_synthetic_psd(path, 32, 32, duplicates)) {
            std::cerr << "Q0b: the synthesiser could not write " << path.generic_string()
                      << ".\n";
            return false;
        }
        marrow::editor::PsdImportResult result;
        if (!mar188::import_synthetic(path, scratch / "duplicates_out", "Q0b", &result)) {
            return false;
        }
        std::vector<std::string> slots;
        for (const marrow::editor::PsdImportedLayer& layer : result.layers) {
            slots.push_back(layer.slot_name);
        }
        std::sort(slots.begin(), slots.end());
        const std::vector<std::string> expected{"body", "torso/body"};
        if (slots != expected) {
            std::cerr << "Q0b: two layers sharing a name must take the joined-path "
                         "dedup branch (expected [body, torso/body], got [";
            for (const std::string& slot : slots) {
                std::cerr << slot << ",";
            }
            std::cerr << "]).\n";
            return false;
        }
    }

    std::cout << "Q0/Q0b: the synthesiser reproduces the checked-in fixture's layer "
                 "and bone reports exactly, its extracted pixels survive the round "
                 "trip, its header agrees with the fixture's on every field that is "
                 "not the canvas, and the duplicate-name dedup branch -- which the "
                 "fixture tree cannot reach -- resolves to the joined group path.\n";
    return true;
}

namespace mar188 {

const char* change_name(marrow::editor::PsdLayerChangeKind change) {
    switch (change) {
        case marrow::editor::PsdLayerChangeKind::Added:
            return "Added";
        case marrow::editor::PsdLayerChangeKind::Updated:
            return "Updated";
        case marrow::editor::PsdLayerChangeKind::Missing:
            return "Missing";
    }
    return "<unknown>";
}

/** @brief The full ORDERED `(identity, kind)` list every classification case asserts. */
std::vector<std::string> plan_rows(const marrow::editor::PsdReimportPlan& plan) {
    std::vector<std::string> rows;
    rows.reserve(plan.layers.size());
    for (const marrow::editor::PsdPlannedLayer& layer : plan.layers) {
        rows.push_back(layer.identity + " -> " + change_name(layer.change));
    }
    return rows;
}

/** @brief A scratch project over the fixture skeleton, carrying given provenance. */
marrow::editor::ProjectData project_with_provenance(
    const std::filesystem::path& project_path,
    const std::optional<marrow::editor::PsdImportProvenance>& provenance) {
    marrow::editor::MinimalProjectOptions options;
    options.project_path = project_path;
    options.skeleton_path = std::filesystem::absolute("assets/fixtures/player_idle.mskl");
    options.atlas_paths = {std::filesystem::absolute("assets/fixtures/player_idle.matl")};
    options.name = "mar188_plan";
    marrow::editor::ProjectData project = marrow::editor::create_minimal_project(options);
    if (provenance.has_value()) {
        marrow::editor::ProjectImportSources sources;
        sources.psd = *provenance;
        project.editor_metadata.import_sources = std::move(sources);
    }
    return project;
}

/** @brief Provenance for the fixture tree, as a real import would store it. */
marrow::editor::PsdImportProvenance fixture_provenance(
    const std::filesystem::path& scratch,
    const std::filesystem::path& project_path,
    bool* ok_out) {
    const std::filesystem::path psd = scratch / "provenance_source.psd";
    *ok_out = false;
    if (!write_synthetic_psd(psd, 64, 64, fixture_tree())) {
        return {};
    }
    marrow::editor::PsdImportResult result;
    if (!import_synthetic(psd, scratch / "provenance_out", "provenance", &result)) {
        return {};
    }
    *ok_out = true;
    return marrow::editor::make_psd_provenance(result, project_path, psd);
}

/** @brief A full sorted recursive listing with sizes, for the zero-mutation clause. */
std::vector<std::string> directory_listing(const std::filesystem::path& root) {
    std::vector<std::string> entries;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            break;
        }
        std::string row = iterator->path().generic_string();
        std::error_code size_error;
        if (iterator->is_regular_file(size_error)) {
            row += " (" + std::to_string(std::filesystem::file_size(iterator->path(), size_error)) +
                " bytes)";
        } else {
            row += " (dir)";
        }
        entries.push_back(std::move(row));
    }
    std::sort(entries.begin(), entries.end());
    return entries;
}

/** @brief Reads a whole file, for the byte-identity clauses. */
std::string read_all(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(
        (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

/** @brief Q8's six clauses, run over a plan attempt that must have changed nothing. */
struct InertnessWitness {
    std::string serialized;
    std::string project_file;
    std::string skeleton_file;
    std::vector<std::string> listing;
    std::filesystem::path project_path;
    std::filesystem::path skeleton_path;
    std::filesystem::path project_directory;
};

InertnessWitness capture_inertness(
    const marrow::editor::ProjectData& project,
    const std::filesystem::path& project_path) {
    InertnessWitness witness;
    witness.project_path = project_path;
    witness.skeleton_path = project.resolved_skeleton_path();
    witness.project_directory = project_path.parent_path();
    witness.serialized = marrow::editor::serialize_project(project);
    witness.project_file = read_all(project_path);
    witness.skeleton_file = read_all(witness.skeleton_path);
    witness.listing = directory_listing(witness.project_directory);
    return witness;
}

bool expect_inert(
    const InertnessWitness& before,
    const marrow::editor::ProjectData& project,
    std::string_view label) {
    if (marrow::editor::serialize_project(project) != before.serialized) {
        std::cerr << label << ": planning changed the project's serialization.\n";
        return false;
    }
    if (read_all(before.project_path) != before.project_file) {
        std::cerr << label << ": planning rewrote the project file on disk.\n";
        return false;
    }
    if (read_all(before.skeleton_path) != before.skeleton_file) {
        std::cerr << label << ": planning rewrote the skeleton file.\n";
        return false;
    }
    const std::vector<std::string> after = directory_listing(before.project_directory);
    if (after != before.listing) {
        std::cerr << label << ": planning changed the project directory.\n";
        for (const std::string& row : before.listing) {
            if (std::find(after.begin(), after.end(), row) == after.end()) {
                std::cerr << "  missing: " << row << '\n';
            }
        }
        for (const std::string& row : after) {
            if (std::find(before.listing.begin(), before.listing.end(), row) ==
                before.listing.end()) {
                std::cerr << "  created: " << row << '\n';
            }
        }
        return false;
    }
    return true;
}

bool expect_staged_under_root(
    const marrow::editor::PsdReimportPlan& plan,
    std::string_view label) {
    const std::string root = plan.staging_root.lexically_normal().generic_string();
    const std::vector<std::pair<const char*, std::filesystem::path>> staged = {
        {"staged_skeleton_path", plan.staged_skeleton_path},
        {"staged_atlas_path", plan.staged_atlas_path},
        {"staged_layers_directory", plan.staged_layers_directory},
    };
    for (const auto& entry : staged) {
        const std::string value = entry.second.lexically_normal().generic_string();
        if (value.rfind(root, 0) != 0U) {
            std::cerr << label << ": " << entry.first
                      << " escaped the staging root (root " << root << ", got " << value
                      << ").\n";
            return false;
        }
    }
    return true;
}

}  // namespace mar188

/**
 * @brief Q1-Q11 -- classification, refusals, inertness, determinism.
 *
 * Every classification case asserts the full ORDERED `(identity, kind)` list. The
 * three counts appear only as a redundant clause, because a count a mutation
 * cannot change is not an assertion -- and the ordered form is what sees a plan
 * emitted in candidate-record order, which a set difference cannot.
 */
bool validate_mar188_reimport_planning(const std::filesystem::path& scratch) {
    using marrow::editor::PsdLayerChangeKind;
    std::error_code directory_error;
    // A staging root left behind by a previous run is refused by design, so a
    // stale scratch directory would surface as a failure of whichever case ran
    // first rather than as the harness problem it is.
    std::filesystem::remove_all(scratch, directory_error);
    std::filesystem::create_directories(scratch, directory_error);

    const std::filesystem::path project_directory = scratch / "project";
    std::filesystem::create_directories(project_directory, directory_error);
    const std::filesystem::path project_path = project_directory / "plan.marrow";

    bool provenance_ok = false;
    const marrow::editor::PsdImportProvenance provenance =
        mar188::fixture_provenance(scratch / "prov", project_path, &provenance_ok);
    if (!provenance_ok) {
        std::cerr << "Q1: could not build the provenance fixture.\n";
        return false;
    }
    const marrow::editor::ProjectData project =
        mar188::project_with_provenance(project_path, provenance);
    {
        const auto saved = marrow::editor::save_project(project, project_path);
        if (!saved) {
            std::cerr << "Q1: could not save the scratch project: "
                      << saved.error->format() << '\n';
            return false;
        }
    }

    std::size_t staging_index = 0;
    const auto fresh_staging = [&]() {
        return scratch / ("staging_" + std::to_string(staging_index++));
    };

    const auto plan_for = [&](const std::vector<mar188::SynthLayer>& layers,
                              const std::string& name,
                              marrow::editor::PsdReimportPlan* plan_out) {
        const std::filesystem::path psd = scratch / (name + ".psd");
        if (!mar188::write_synthetic_psd(psd, 64, 64, layers)) {
            std::cerr << name << ": the synthesiser could not write the candidate.\n";
            return false;
        }
        marrow::editor::PsdReimportPlanOptions options;
        options.psd_path = psd;
        options.staging_root = fresh_staging();
        *plan_out = marrow::editor::plan_psd_reimport(project, options);
        return true;
    };

    // -- Q1 -- classification, full ordered list. ----------------------------
    {
        const std::vector<mar188::SynthLayer> candidate = {
            {{}, "shadow", 18, 40, 14, 8, 10U, 20U, 30U},
            {{"torso"}, "body", 20, 14, 20, 24, 70U, 80U, 90U},
            {{"torso"}, "hand_r", 30, 30, 6, 6, 11U, 12U, 13U},
        };
        marrow::editor::PsdReimportPlan plan;
        if (!plan_for(candidate, "q1", &plan)) {
            return false;
        }
        if (!plan) {
            std::cerr << "Q1: planning failed: " << plan.error->format() << '\n';
            return false;
        }
        const std::vector<std::string> expected = {
            "shadow -> Updated",
            "torso|arm_l -> Missing",
            "torso|body -> Updated",
            "torso|hand_r -> Added",
        };
        if (!mar188::expect_rows("Q1 (ordered identity/kind list)", mar188::plan_rows(plan),
                                 expected)) {
            return false;
        }
        // Redundant, and deliberately so: I17 accumulates counts in a parallel
        // loop that misses an arm, which every list clause survives.
        if (plan.added_count != 1U || plan.updated_count != 2U || plan.missing_count != 1U) {
            std::cerr << "Q1: plan counts must match the classified list (added expected "
                         "1, got "
                      << plan.added_count << "; updated expected 2, got "
                      << plan.updated_count << "; missing expected 1, got "
                      << plan.missing_count << ").\n";
            return false;
        }
        for (const marrow::editor::PsdPlannedLayer& layer : plan.layers) {
            if (layer.change == PsdLayerChangeKind::Missing) {
                continue;
            }
            if (layer.proposed_slot_name.empty() || layer.proposed_attachment_name.empty() ||
                layer.proposed_bone_name.empty() || layer.proposed_image_file.empty()) {
                std::cerr << "Q1: an " << mar188::change_name(layer.change)
                          << " layer must carry proposed targets (" << layer.identity
                          << ": slot='" << layer.proposed_slot_name << "' attachment='"
                          << layer.proposed_attachment_name << "' bone='"
                          << layer.proposed_bone_name << "' image='"
                          << layer.proposed_image_file << "').\n";
                return false;
            }
        }
        // The full stored tuple, which is what the six-assignment provenance copy
        // is checked by -- a compiler-blind fixed-length list.
        for (const marrow::editor::PsdPlannedLayer& layer : plan.layers) {
            if (layer.identity != "torso|body") {
                continue;
            }
            if (layer.current_slot_name != "body" || layer.current_attachment_name != "body" ||
                layer.current_bone_name != "torso" || layer.current_image_file != "body.png") {
                std::cerr << "Q1: the stored provenance tuple must survive into the plan "
                             "(torso|body: slot='"
                          << layer.current_slot_name << "' attachment='"
                          << layer.current_attachment_name << "' bone='"
                          << layer.current_bone_name << "' image='"
                          << layer.current_image_file
                          << "', expected body/body/torso/body.png).\n";
                return false;
            }
        }
    }

    // -- Q2 -- a rename is TWO events, never one. ----------------------------
    {
        const std::vector<mar188::SynthLayer> candidate = {
            {{}, "shadow", 18, 40, 14, 8, 10U, 20U, 30U},
            {{"torso"}, "arm_left", 4, 20, 12, 8, 40U, 50U, 60U},
            {{"torso"}, "body", 16, 12, 20, 24, 70U, 80U, 90U},
        };
        marrow::editor::PsdReimportPlan plan;
        if (!plan_for(candidate, "q2", &plan) || !plan) {
            std::cerr << "Q2: planning failed.\n";
            return false;
        }
        const std::vector<std::string> expected = {
            "shadow -> Updated",
            "torso|arm_l -> Missing",
            "torso|arm_left -> Added",
            "torso|body -> Updated",
        };
        if (!mar188::expect_rows("Q2 (a rename must never be inferred)",
                                 mar188::plan_rows(plan), expected)) {
            return false;
        }
    }

    // -- Q3 -- a group move is two events. -----------------------------------
    {
        const std::vector<mar188::SynthLayer> candidate = {
            {{}, "body", 16, 12, 20, 24, 70U, 80U, 90U},
            {{}, "shadow", 18, 40, 14, 8, 10U, 20U, 30U},
            {{"torso"}, "arm_l", 4, 20, 12, 8, 40U, 50U, 60U},
        };
        marrow::editor::PsdReimportPlan plan;
        if (!plan_for(candidate, "q3", &plan) || !plan) {
            std::cerr << "Q3: planning failed.\n";
            return false;
        }
        const std::vector<std::string> expected = {
            "body -> Added",
            "shadow -> Updated",
            "torso|arm_l -> Updated",
            "torso|body -> Missing",
        };
        if (!mar188::expect_rows("Q3 (a group move must produce one Added and one Missing)",
                                 mar188::plan_rows(plan), expected)) {
            return false;
        }
    }

    // -- Q4 -- depth-2 nesting, end to end. ----------------------------------
    // The first coverage a group nested deeper than one level has ever had in
    // this repository: no such fixture exists, so the inheritance walk and the
    // parent-relative bone offsets were unexercised before this case.
    {
        const std::vector<mar188::SynthLayer> candidate = {
            {{}, "shadow", 18, 40, 14, 8, 10U, 20U, 30U},
            {{"torso"}, "arm_l", 4, 20, 12, 8, 40U, 50U, 60U},
            {{"torso"}, "body", 16, 12, 20, 24, 70U, 80U, 90U},
            {{"torso", "upper"}, "hand", 30, 30, 6, 6, 14U, 15U, 16U},
        };
        marrow::editor::PsdReimportPlan plan;
        if (!plan_for(candidate, "q4", &plan) || !plan) {
            std::cerr << "Q4: planning failed.\n";
            return false;
        }
        const marrow::editor::PsdPlannedLayer* hand = nullptr;
        for (const marrow::editor::PsdPlannedLayer& layer : plan.layers) {
            if (layer.layer_name == "hand") {
                hand = &layer;
            }
        }
        if (hand == nullptr) {
            std::cerr << "Q4: the depth-2 layer did not reach the plan.\n";
            return false;
        }
        const std::vector<std::string> expected_group{"torso", "upper"};
        if (hand->group_path != expected_group) {
            std::string got;
            for (const std::string& segment : hand->group_path) {
                got += segment + ",";
            }
            std::cerr << "Q4: a depth-2 group_path must survive element-wise "
                         "(expected [torso,upper,], got ["
                      << got << "]).\n";
            return false;
        }
        if (hand->identity != "torso|upper|hand") {
            std::cerr << "Q4: a depth-2 identity must be torso|upper|hand, got "
                      << hand->identity << ".\n";
            return false;
        }
        if (hand->change != PsdLayerChangeKind::Added || hand->proposed_bone_name.empty()) {
            std::cerr << "Q4: the depth-2 layer must be Added with a proposed bone, got "
                      << mar188::change_name(hand->change) << " bone='"
                      << hand->proposed_bone_name << "'.\n";
            return false;
        }
    }

    // -- Q5 -- escaped identities do not collide. ----------------------------
    // Unlike MAR-186's families, the collision here is CONSTRUCTIBLE: every token
    // is a free Photoshop string and the arity varies with nesting depth.
    {
        const std::vector<mar188::SynthLayer> candidate = {
            {{"a"}, "b|c", 0, 0, 4, 4, 1U, 2U, 3U},
            {{"a|b"}, "c", 8, 8, 4, 4, 4U, 5U, 6U},
        };
        marrow::editor::PsdReimportPlan plan;
        if (!plan_for(candidate, "q5", &plan) || !plan) {
            std::cerr << "Q5: planning failed.\n";
            return false;
        }
        std::vector<std::string> identities;
        for (const marrow::editor::PsdPlannedLayer& layer : plan.layers) {
            if (layer.change == PsdLayerChangeKind::Added) {
                identities.push_back(layer.identity);
            }
        }
        std::sort(identities.begin(), identities.end());
        const std::vector<std::string> expected{"a\\|b|c", "a|b\\|c"};
        if (identities != expected) {
            std::cerr << "Q5: escaped identities must not collide (expected 2 distinct "
                         "entries [a\\|b|c, a|b\\|c], got "
                      << identities.size() << " [";
            for (const std::string& identity : identities) {
                std::cerr << identity << ",";
            }
            std::cerr << "]).\n";
            return false;
        }
    }

    // -- Q6 -- a duplicate identity in the candidate is REFUSED. -------------
    {
        const std::vector<mar188::SynthLayer> candidate = {
            {{"torso"}, "body", 0, 0, 4, 4, 1U, 2U, 3U},
            {{"torso"}, "body", 8, 8, 4, 4, 4U, 5U, 6U},
        };
        const mar188::InertnessWitness before =
            mar188::capture_inertness(project, project_path);
        marrow::editor::PsdReimportPlan plan;
        if (!plan_for(candidate, "q6", &plan)) {
            return false;
        }
        if (plan) {
            std::cerr << "Q6: duplicate candidate identities must be refused, got a "
                         "successful plan.\n";
            return false;
        }
        const std::string text = plan.error->format();
        if (text.find("psd layers must have unique group and name identities") ==
            std::string::npos) {
            std::cerr << "Q6: the refusal must name its cause. Actual: " << text << '\n';
            return false;
        }
        if (text.find("torso|body") == std::string::npos) {
            std::cerr << "Q6: the refusal must name the colliding identity. Actual: "
                      << text << '\n';
            return false;
        }
        if (!mar188::expect_inert(before, project, "Q6")) {
            return false;
        }
    }

    // -- Q7 -- a parse failure is an error, not a throw, and writes nothing. --
    {
        const std::filesystem::path text_file = scratch / "not_a_psd.psd";
        {
            std::ofstream stream(text_file, std::ios::binary | std::ios::trunc);
            stream << "this is definitely not a photoshop document";
        }
        const mar188::InertnessWitness before =
            mar188::capture_inertness(project, project_path);
        marrow::editor::PsdReimportPlanOptions options;
        options.psd_path = text_file;
        options.staging_root = fresh_staging();
        const marrow::editor::PsdReimportPlan plan =
            marrow::editor::plan_psd_reimport(project, options);
        if (plan) {
            std::cerr << "Q7: a non-PSD candidate must be refused.\n";
            return false;
        }
        const std::string text = plan.error->format();
        if (text.find("PSD files must begin with the 8BPS signature.") == std::string::npos) {
            std::cerr << "Q7: the refusal must carry the parser's own message. Actual: "
                      << text << '\n';
            return false;
        }
        if (text.find(text_file.string()) == std::string::npos) {
            std::cerr << "Q7: the refusal must name the candidate path. Actual: " << text
                      << '\n';
            return false;
        }
        if (std::filesystem::exists(options.staging_root / "staged.mskl")) {
            std::cerr << "Q7: a failed plan must not leave a staged skeleton behind.\n";
            return false;
        }
        if (!mar188::expect_inert(before, project, "Q7")) {
            return false;
        }
    }

    // -- Q8 -- zero mutation, on the SUCCESS path. ---------------------------
    {
        const mar188::InertnessWitness before =
            mar188::capture_inertness(project, project_path);
        marrow::editor::PsdReimportPlan plan;
        if (!plan_for(mar188::fixture_tree(), "q8", &plan) || !plan) {
            std::cerr << "Q8: planning failed.\n";
            return false;
        }
        if (!mar188::expect_inert(before, project, "Q8")) {
            return false;
        }
        if (!mar188::expect_staged_under_root(plan, "Q8")) {
            return false;
        }

        // The staged merge must behave like a REAL reimport, which is the whole
        // reason `existing_skeleton_path` is set explicitly rather than left to
        // `effective_existing_skeleton_path`'s fallback. Left unset the importer
        // reads the STAGING skeleton -- which does not exist -- and produces a
        // skeleton with none of the project's authored animations.
        //
        // Nothing else in this story observes that: every `proposed_*` target
        // comes from the CANDIDATE parse, not from the merge, so the plan's own
        // contents are identical either way. Measured, not assumed.
        {
            const std::string staged = mar188::read_all(plan.staged_skeleton_path);
            if (staged.empty()) {
                std::cerr << "Q8: no staged skeleton was written.\n";
                return false;
            }
            for (const char* animation : {"idle", "attack", "aim"}) {
                if (staged.find(std::string("\"") + animation + "\"") == std::string::npos) {
                    std::cerr << "Q8: the staged skeleton must carry the project's "
                                 "authored animation '"
                              << animation
                              << "' -- planning merged against the wrong existing "
                                 "skeleton.\n";
                    return false;
                }
            }
        }
    }

    // -- Q9 -- Missing defaults to preservation. -----------------------------
    {
        const std::vector<mar188::SynthLayer> candidate = {
            {{}, "shadow", 18, 40, 14, 8, 10U, 20U, 30U},
            {{"torso"}, "body", 16, 12, 20, 24, 70U, 80U, 90U},
        };
        marrow::editor::PsdReimportPlan plan;
        if (!plan_for(candidate, "q9", &plan) || !plan) {
            std::cerr << "Q9: planning failed.\n";
            return false;
        }
        bool saw_missing = false;
        for (const marrow::editor::PsdPlannedLayer& layer : plan.layers) {
            if (layer.change != PsdLayerChangeKind::Missing) {
                continue;
            }
            saw_missing = true;
            if (!layer.preserve) {
                std::cerr << "Q9: a Missing layer must default to preservation ("
                          << layer.identity << ": preserve expected true, got false).\n";
                return false;
            }
            if (!layer.proposed_slot_name.empty() ||
                !layer.proposed_attachment_name.empty() ||
                !layer.proposed_bone_name.empty() || !layer.proposed_image_file.empty()) {
                std::cerr << "Q9: a Missing layer must carry no proposed targets ("
                          << layer.identity << ").\n";
                return false;
            }
        }
        if (!saw_missing) {
            std::cerr << "Q9: the fixture produced no Missing layer to assert on.\n";
            return false;
        }
    }

    // -- Q10 -- determinism, element-wise and IN ORDER. ----------------------
    // A set difference is order-free and cannot see a plan emitted in
    // candidate-record order; this clause and Q1's ordered list are what can.
    {
        marrow::editor::PsdReimportPlan first;
        marrow::editor::PsdReimportPlan second;
        if (!plan_for(mar188::fixture_tree(), "q10a", &first) || !first ||
            !plan_for(mar188::fixture_tree(), "q10b", &second) || !second) {
            std::cerr << "Q10: planning failed.\n";
            return false;
        }
        if (first.layers.size() != second.layers.size()) {
            std::cerr << "Q10: two independent plans differ in size (" << first.layers.size()
                      << " vs " << second.layers.size() << ").\n";
            return false;
        }
        for (std::size_t index = 0; index < first.layers.size(); ++index) {
            const marrow::editor::PsdPlannedLayer& a = first.layers[index];
            const marrow::editor::PsdPlannedLayer& b = second.layers[index];
            if (a.identity != b.identity || a.change != b.change ||
                a.current_slot_name != b.current_slot_name ||
                a.current_attachment_name != b.current_attachment_name ||
                a.current_bone_name != b.current_bone_name ||
                a.current_image_file != b.current_image_file ||
                a.proposed_slot_name != b.proposed_slot_name ||
                a.proposed_attachment_name != b.proposed_attachment_name ||
                a.proposed_bone_name != b.proposed_bone_name ||
                a.proposed_image_file != b.proposed_image_file ||
                a.preserve != b.preserve) {
                std::cerr << "Q10: two independent plans must be element-wise identical "
                             "in order (index "
                          << index << ": expected " << a.identity << ", got " << b.identity
                          << ").\n";
                return false;
            }
        }
        std::vector<std::string> identities;
        for (const marrow::editor::PsdPlannedLayer& layer : first.layers) {
            identities.push_back(layer.identity);
        }
        std::vector<std::string> sorted = identities;
        std::sort(sorted.begin(), sorted.end());
        if (identities != sorted) {
            std::cerr << "Q10: the plan must be in ascending identity order.\n";
            return false;
        }
    }

    // -- Q11 -- no provenance means a first import. --------------------------
    // This is what makes AC1's word "Optional" honest.
    {
        const std::filesystem::path bare_directory = scratch / "bare";
        std::filesystem::create_directories(bare_directory, directory_error);
        const marrow::editor::ProjectData bare =
            mar188::project_with_provenance(bare_directory / "bare.marrow", std::nullopt);
        const std::filesystem::path psd = scratch / "q11.psd";
        if (!mar188::write_synthetic_psd(psd, 64, 64, mar188::fixture_tree())) {
            std::cerr << "Q11: the synthesiser could not write the candidate.\n";
            return false;
        }
        marrow::editor::PsdReimportPlanOptions options;
        options.psd_path = psd;
        options.staging_root = fresh_staging();
        const marrow::editor::PsdReimportPlan plan =
            marrow::editor::plan_psd_reimport(bare, options);
        if (!plan) {
            std::cerr << "Q11: planning failed: " << plan.error->format() << '\n';
            return false;
        }
        const std::vector<std::string> expected = {
            "shadow -> Added",
            "torso|arm_l -> Added",
            "torso|body -> Added",
        };
        if (!mar188::expect_rows("Q11 (a project with no provenance)",
                                 mar188::plan_rows(plan), expected)) {
            return false;
        }
        if (plan.added_count != 3U || plan.updated_count != 0U || plan.missing_count != 0U) {
            std::cerr << "Q11: a first import must be all Added (got " << plan.added_count
                      << "/" << plan.updated_count << "/" << plan.missing_count << ").\n";
            return false;
        }
        for (const marrow::editor::PsdPlannedLayer& layer : plan.layers) {
            if (!layer.current_slot_name.empty() || !layer.current_attachment_name.empty() ||
                !layer.current_bone_name.empty() || !layer.current_image_file.empty()) {
                std::cerr << "Q11: an Added layer must carry no current targets ("
                          << layer.identity << ").\n";
                return false;
            }
        }
    }

    // -- The staging-root guards, asserted directly (not inverted). ----------
    {
        marrow::editor::PsdReimportPlanOptions options;
        options.psd_path = scratch / "q8.psd";
        options.staging_root.clear();
        const marrow::editor::PsdReimportPlan plan =
            marrow::editor::plan_psd_reimport(project, options);
        if (plan || plan.error->format().find("staging root must not be empty") ==
                        std::string::npos) {
            std::cerr << "Q8(guard): an empty staging root must be refused by name.\n";
            return false;
        }
    }
    {
        const std::filesystem::path occupied = fresh_staging();
        std::filesystem::create_directories(occupied, directory_error);
        {
            std::ofstream stream(occupied / "someone_elses.txt");
            stream << "not mine";
        }
        marrow::editor::PsdReimportPlanOptions options;
        options.psd_path = scratch / "q8.psd";
        options.staging_root = occupied;
        const marrow::editor::PsdReimportPlan plan =
            marrow::editor::plan_psd_reimport(project, options);
        if (plan || plan.error->format().find("staging root must be empty or absent") ==
                        std::string::npos) {
            std::cerr << "Q8(guard): a non-empty staging root must be refused by name.\n";
            return false;
        }
        if (!std::filesystem::exists(occupied / "someone_elses.txt")) {
            std::cerr << "Q8(guard): the refusal deleted the caller's own file.\n";
            return false;
        }
    }

    std::cout << "MAR-188 Q1-Q11: classification is exact-name only -- a rename and a "
                 "group move each produce one Added and one Missing rather than an "
                 "Updated, depth-2 nesting survives element-wise, escaped identities "
                 "stay distinct where the collision is constructible, duplicate "
                 "candidates and a non-PSD are refused by name, a plan leaves the "
                 "project's serialization, files and whole directory listing "
                 "byte-identical with every staged path under the caller's root, "
                 "Missing defaults to preservation, two plans agree element-wise in "
                 "ascending identity order, and a project with no provenance plans "
                 "every layer as Added.\n";
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
    // Q0 runs BEFORE every other MAR-188 case so a synthesiser regression is
    // attributed to the gate rather than to whichever case happens to notice.
    if (!validate_mar188_q0(options.initial_psd,
                            std::filesystem::temp_directory_path() / "mar188_q0")) {
        return 1;
    }
    if (!validate_mar188_reimport_planning(
            std::filesystem::temp_directory_path() / "mar188_plan")) {
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
