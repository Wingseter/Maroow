#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>
#include <vector>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

#include "atlas_packer.hpp"
#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/editor/psd_reimport_commit.hpp"
#include "marrow/editor/psd_reimport_plan.hpp"
#include "marrow/editor/psd_reimport_review.hpp"
#include "marrow/editor/psd_import.hpp"
#include "marrow/editor/session.hpp"
#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/skeleton.hpp"
#include "psd_reimport_commit_internal.hpp"
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

/**
 * @brief A scratch root private to THIS process.
 *
 * Every root in this file used to be a fixed path under `temp_directory_path()`,
 * and the cases `remove_all` their roots on entry -- so two concurrent runs of
 * this binary delete each other's trees mid-run. That is not hypothetical:
 * `marrow.psd_import_smoke` FAILED in its first full `ctest` and passed on an
 * isolated rerun, which is the signature of exactly this and is also the
 * signature of a flake worth ignoring. It was neither.
 *
 * A green result that depends on nobody else running the same binary is not a
 * result. The pid suffix is what makes the recorded 23/23 reproducible under the
 * concurrency this repository actually has.
 */
std::filesystem::path scratch_root(std::string_view name) {
#if defined(_WIN32)
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(::getpid());
#endif
    return std::filesystem::temp_directory_path() /
        (std::string(name) + "-" + std::to_string(pid));
}

/**
 * @brief The agent cases' root, under `/tmp` and NOT `temp_directory_path()`.
 *
 * `agent_path_allowed` whitelists `/tmp` and `/private/tmp` and does not whitelist
 * `$TMPDIR`, so an A-case sited under `temp_directory_path()` is refused as a
 * forbidden input path before it can test anything. Per-process for the same
 * reason as `scratch_root`.
 */
std::filesystem::path agent_scratch_root() {
#if defined(_WIN32)
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(::getpid());
#endif
    return std::filesystem::path("/tmp") / ("mar189_agent-" + std::to_string(pid));
}

/**
 * @brief Owns a scratch root: removes it on success, KEEPS it on failure.
 *
 * The per-process roots that fixed the concurrency flake also removed the thing
 * that had been cleaning up by accident -- the fixed roots were REUSED, so each
 * run's `remove_all` on entry disposed of the last one's tree. Making the names
 * unique removed the reuse and therefore removed the disposal, and nothing
 * replaced it: measured at **615 leaked roots, 299 MB**, growing by ~2 MB per
 * `ctest` and per inversion, permanently.
 *
 * Keeping the tree on FAILURE is deliberate, not the same leak in a smaller form:
 * a failing case is exactly when someone wants to look at the bundle it built, and
 * the path is printed so they can. Leaking on failure by design is defensible;
 * leaking on success is not.
 */
class ScratchRoot {
public:
    explicit ScratchRoot(std::filesystem::path path) : path_(std::move(path)) {}
    ~ScratchRoot() {
        std::error_code error;
        if (!keep_) {
            std::filesystem::remove_all(path_, error);
            return;
        }
        std::cerr << "  scratch kept for inspection: " << path_.generic_string() << '\n';
    }
    ScratchRoot(const ScratchRoot&) = delete;
    ScratchRoot& operator=(const ScratchRoot&) = delete;

    const std::filesystem::path& path() const {
        return path_;
    }
    /// @brief Do not dispose of this root; the run failed and the tree is evidence.
    void keep() {
        keep_ = true;
    }

private:
    std::filesystem::path path_;
    bool keep_{false};
};

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

namespace mar189 {

/** @brief Reads one string member of a `.matl`'s `atlas` object, or `<absent>`. */
std::string atlas_member(const std::filesystem::path& atlas_path, const char* key) {
    const marrow::runtime::json::LoadResult loaded =
        marrow::runtime::json::load_document(atlas_path);
    if (!loaded) {
        return "<unparsable: " + loaded.error->message + ">";
    }
    const marrow::runtime::json::Value* atlas =
        marrow::runtime::json::find_member(loaded.document->root, "atlas");
    if (atlas == nullptr || !atlas->is_object()) {
        return "<no atlas object>";
    }
    const marrow::runtime::json::Value* member =
        marrow::runtime::json::find_member(*atlas, key);
    if (member == nullptr || !member->is_string()) {
        return "<absent>";
    }
    return member->as_string();
}

}  // namespace mar189

/**
 * @brief Q12-Q14 -- the staged bundle is named after the bundle it will replace.
 *
 * Q13 is the gate. Q12 is a compatibility WITNESS: every clause in it is green
 * before MAR-189 exists, because it asserts MAR-188's defaults are unchanged.
 * Its one MAR-189-owned clause is the new `staged_texture_path` field, which
 * does not compile before this story and therefore cannot witness anything
 * either -- recorded rather than counted as coverage.
 */
bool validate_mar189_staged_naming(const std::filesystem::path& scratch) {
    std::error_code directory_error;
    std::filesystem::remove_all(scratch, directory_error);
    directory_error.clear();
    std::filesystem::create_directories(scratch, directory_error);
    if (directory_error) {
        std::cerr << "Q12-Q14: scratch directory could not be created: "
                  << directory_error.message() << '\n';
        return false;
    }

    const std::filesystem::path psd = scratch / "candidate.psd";
    if (!mar188::write_synthetic_psd(psd, 64, 64, mar188::fixture_tree())) {
        std::cerr << "Q12-Q14: the synthetic PSD could not be written.\n";
        return false;
    }
    const std::filesystem::path project_path = scratch / "project" / "named.marrow";
    const marrow::editor::ProjectData project =
        mar188::project_with_provenance(project_path, std::nullopt);

    // Q12 -- defaults unchanged. Witness.
    {
        marrow::editor::PsdReimportPlanOptions options;
        options.psd_path = psd;
        options.staging_root = scratch / "q12";
        const marrow::editor::PsdReimportPlan plan =
            marrow::editor::plan_psd_reimport(project, options);
        if (!plan) {
            std::cerr << "Q12: the default plan must succeed; got "
                      << plan.error->format() << '\n';
            return false;
        }
        const std::vector<std::pair<const char*, std::string>> expected = {
            {"staged_skeleton_path", "staged.mskl"},
            {"staged_atlas_path", "staged.matl"},
            {"staged_texture_path", "staged.png"},
            {"staged_layers_directory", "staged_layers"},
        };
        const std::vector<std::filesystem::path> actual = {
            plan.staged_skeleton_path,
            plan.staged_atlas_path,
            plan.staged_texture_path,
            plan.staged_layers_directory,
        };
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (actual[index].filename().generic_string() != expected[index].second) {
                std::cerr << "Q12: " << expected[index].first << " must default to '"
                          << expected[index].second << "'; got '"
                          << actual[index].generic_string() << "'.\n";
                return false;
            }
        }
        if (mar189::atlas_member(plan.staged_atlas_path, "image") != "staged.png") {
            std::cerr << "Q12: the default staged atlas must still say "
                         "\"image\": \"staged.png\"; got '"
                      << mar189::atlas_member(plan.staged_atlas_path, "image") << "'.\n";
            return false;
        }
    }

    // Q13 -- THE GATE. The staged bundle carries the target's own names, and the
    // atlas document's own `image` member is what says so. I6 reverts the naming
    // and this clause is the only one in the tree that reddens.
    {
        marrow::editor::PsdReimportPlanOptions options;
        options.psd_path = psd;
        options.staging_root = scratch / "q13";
        options.staged_skeleton_filename = "player_idle.mskl";
        options.staged_atlas_filename = "player_fixture.matl";
        const marrow::editor::PsdReimportPlan plan =
            marrow::editor::plan_psd_reimport(project, options);
        if (!plan) {
            std::cerr << "Q13: a plan under target names must succeed; got "
                      << plan.error->format() << '\n';
            return false;
        }
        const std::vector<std::pair<const char*, std::string>> expected = {
            {"staged_skeleton_path", "player_idle.mskl"},
            {"staged_atlas_path", "player_fixture.matl"},
            {"staged_texture_path", "player_fixture.png"},
        };
        const std::vector<std::filesystem::path> actual = {
            plan.staged_skeleton_path,
            plan.staged_atlas_path,
            plan.staged_texture_path,
        };
        const std::string root = options.staging_root.lexically_normal().generic_string();
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (actual[index].filename().generic_string() != expected[index].second) {
                std::cerr << "Q13: " << expected[index].first << " must be named '"
                          << expected[index].second << "'; got '"
                          << actual[index].generic_string() << "'.\n";
                return false;
            }
            if (actual[index].lexically_normal().generic_string().rfind(root, 0) != 0U) {
                std::cerr << "Q13: " << expected[index].first
                          << " escaped the staging root (root " << root << ", got "
                          << actual[index].generic_string() << ").\n";
                return false;
            }
            if (!std::filesystem::exists(actual[index])) {
                std::cerr << "Q13: " << expected[index].first << " ('"
                          << actual[index].generic_string() << "') was not written.\n";
                return false;
            }
        }
        const std::string image = mar189::atlas_member(plan.staged_atlas_path, "image");
        if (image != "player_fixture.png") {
            std::cerr << "Q13: staged atlas references image '" << image
                      << "'; expected 'player_fixture.png'. A byte copy of this file "
                         "onto a project atlas would name a PNG that is not beside it.\n";
            return false;
        }
        const std::string name = mar189::atlas_member(plan.staged_atlas_path, "name");
        if (name != "player_fixture") {
            std::cerr << "Q13: staged atlas is named '" << name
                      << "'; expected 'player_fixture'.\n";
            return false;
        }
    }

    // Q14 -- a name that is not a bare file name is refused, by message.
    {
        const std::vector<std::pair<std::string, std::string>> rejected = {
            {"../escape.matl", "staged atlas file name must be a bare file name (../escape.matl)"},
            {"nested/atlas.matl", "staged atlas file name must be a bare file name (nested/atlas.matl)"},
            {"", "staged atlas file name must not be empty"},
        };
        for (std::size_t index = 0; index < rejected.size(); ++index) {
            marrow::editor::PsdReimportPlanOptions options;
            options.psd_path = psd;
            options.staging_root = scratch / ("q14_" + std::to_string(index));
            options.staged_atlas_filename = rejected[index].first;
            const marrow::editor::PsdReimportPlan plan =
                marrow::editor::plan_psd_reimport(project, options);
            if (plan) {
                std::cerr << "Q14: atlas file name '" << rejected[index].first
                          << "' must be refused; the plan succeeded.\n";
                return false;
            }
            if (plan.error->message != rejected[index].second) {
                std::cerr << "Q14: atlas file name '" << rejected[index].first
                          << "' must be refused with '" << rejected[index].second
                          << "'; got '" << plan.error->message << "'.\n";
                return false;
            }
        }
        marrow::editor::PsdReimportPlanOptions skeleton_options;
        skeleton_options.psd_path = psd;
        skeleton_options.staging_root = scratch / "q14_skeleton";
        skeleton_options.staged_skeleton_filename = "nested/skeleton.mskl";
        const marrow::editor::PsdReimportPlan skeleton_plan =
            marrow::editor::plan_psd_reimport(project, skeleton_options);
        const std::string expected_message =
            "staged skeleton file name must be a bare file name (nested/skeleton.mskl)";
        if (skeleton_plan || skeleton_plan.error->message != expected_message) {
            std::cerr << "Q14: a nested skeleton file name must be refused with '"
                      << expected_message << "'; got '"
                      << (skeleton_plan ? std::string("<no error>")
                                        : skeleton_plan.error->message)
                      << "'.\n";
            return false;
        }
    }

    std::cout << "MAR-189 Q12-Q14: staged bundles default to MAR-188's names, stage "
                 "under a caller-supplied target name with the atlas document's own "
                 "\"image\" and \"name\" members following the stem, report the "
                 "packer-derived texture path, and refuse any name that is not a bare "
                 "file name.\n";
    return true;
}

namespace mar189 {

// ---------------------------------------------------------------------------
// A11 -- the repository-safety gate.
//
// This story's subject is the atomic replacement of asset bundles, and
// `agent_path_allowed` whitelists the PROJECT directory. The agent smoke runs
// with `WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}` against
// `assets/fixtures/player_idle.marrow`, whose `.mskl`, `.matl` and `.png` are
// TRACKED files. A committing case pointed at that session overwrites the user's
// repository, and the blast radius is not a red test.
//
// So this aborts. Not a warning, not a `return false`.
// ---------------------------------------------------------------------------

/**
 * @brief Locates the repository root by walking up for the tracked fixture.
 *
 * NOT by a compiled-in path, and not by `.git` either: an isolated verification
 * tree extracted with `git archive` has no `.git`, and a gate that silently
 * cannot find its root is a gate that passes on the input it exists to catch.
 * The anchor is a tracked file whose presence defines the hazard.
 */
std::filesystem::path repository_root() {
    std::error_code error;
    std::filesystem::path directory = std::filesystem::current_path(error);
    if (error) {
        return {};
    }
    for (;;) {
        if (std::filesystem::exists(directory / "assets" / "fixtures" / "player_idle.marrow",
                                    error)) {
            return directory;
        }
        const std::filesystem::path parent = directory.parent_path();
        if (parent.empty() || parent == directory) {
            break;
        }
        directory = parent;
    }
    // No fixture above the working directory. The working directory itself is
    // still a real place that must not be written into.
    return std::filesystem::current_path(error);
}

bool path_within(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    if (root.empty()) {
        return false;
    }
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end() || *root_part != *candidate_part) {
            return false;
        }
    }
    return true;
}

/**
 * @brief Empty when @p target may be written; otherwise the reason it may not.
 *
 * Clause ORDER is load-bearing and was wrong in the plan. With the temp-directory
 * clause first, every repository path trips it before the repository clause is
 * reached, so the repository clause is dead code and its message -- the one that
 * names the repository root -- is unreachable. Checked repository-first, both
 * clauses are live and both are demonstrated by the self-test below.
 *
 * The allowed set is TMPDIR plus `/tmp` and `/private/tmp`. `temp_directory_path()`
 * on macOS is `$TMPDIR` (`/var/folders/...`), which `agent_path_allowed`
 * (`agent_dispatch.cpp:626-629`) does NOT whitelist, while `/tmp` -- which it does
 * -- is not `temp_directory_path()`. A gate written against either one alone is
 * disjoint from the other and no case can satisfy both.
 */
std::string disposable_target_refusal(const std::filesystem::path& target) {
    std::error_code error;
    const std::filesystem::path resolved = std::filesystem::weakly_canonical(target, error);
    const std::filesystem::path candidate = error ? target.lexically_normal() : resolved;

    // Both repository clauses run BEFORE the disposable-root clause, and the
    // NARROWER of the two runs first. Ordered the other way round each is dead
    // code that the self-test cannot reach: the temp clause swallows every
    // repository path, and the repository clause then swallows every fixture path,
    // so the message naming the artefacts about to be destroyed is unreachable.
    const std::filesystem::path repository =
        std::filesystem::weakly_canonical(repository_root(), error);
    if (!repository.empty() &&
        path_within(candidate, repository / "assets" / "fixtures")) {
        return "'" + candidate.generic_string() +
            "' is inside the tracked fixture directory '" +
            (repository / "assets" / "fixtures").generic_string() + "'.";
    }
    if (!repository.empty() && path_within(candidate, repository)) {
        return "'" + candidate.generic_string() + "' is inside the repository root '" +
            repository.generic_string() +
            "'. Inside the repository is inside the repository, build directory or not.";
    }

    // Clause 1.
    std::vector<std::filesystem::path> allowed;
    const std::filesystem::path temporary =
        std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(error), error);
    if (!temporary.empty()) {
        allowed.push_back(temporary);
    }
    for (const char* literal : {"/tmp", "/private/tmp"}) {
        const std::filesystem::path root =
            std::filesystem::weakly_canonical(std::filesystem::path(literal), error);
        if (!root.empty()) {
            allowed.push_back(root);
        }
    }
    for (const std::filesystem::path& root : allowed) {
        if (path_within(candidate, root)) {
            return {};
        }
    }
    std::string roots;
    for (const std::filesystem::path& root : allowed) {
        roots += (roots.empty() ? "" : ", ") + root.generic_string();
    }
    return "'" + candidate.generic_string() +
        "' is not under any disposable root (" + roots + ").";
}

/// @brief Aborts the process rather than let a case write outside a disposable root.
void require_disposable_target(const std::filesystem::path& target, const char* case_name) {
    const std::string refusal = disposable_target_refusal(target);
    if (refusal.empty()) {
        return;
    }
    std::cerr << "FATAL " << case_name << ": refusing to write outside a disposable "
              << "directory. " << refusal << '\n';
    std::abort();
}

/**
 * @brief Proves the predicate refuses the inputs it exists to refuse.
 *
 * Runs BEFORE any committing case. A predicate that accepts row (b) is worse than
 * no predicate: it is a gate that passes on the exact input it exists to catch.
 * The abort wrapper itself is one unbranched call to this predicate, so what is
 * demonstrated here is the whole of the decision.
 */
bool run_disposable_target_self_test() {
    const std::filesystem::path repository = repository_root();
    if (repository.empty()) {
        std::cerr << "A11: the repository root could not be located.\n";
        return false;
    }
    struct Row {
        const char* label;
        std::filesystem::path target;
        bool accepted;
        const char* must_contain;
    };
    const std::vector<Row> rows = {
        {"a", std::filesystem::temp_directory_path() / "mar189" / "player_idle.mskl", true, ""},
        {"b", repository / "assets" / "fixtures" / "player_idle.mskl", false,
         "is inside the tracked fixture directory"},
        {"c", repository / "build" / "x.mskl", false, "is inside the repository root"},
    };
    for (const Row& row : rows) {
        const std::string refusal = disposable_target_refusal(row.target);
        if (row.accepted) {
            if (!refusal.empty()) {
                std::cerr << "A11(" << row.label << "): '" << row.target.generic_string()
                          << "' must be accepted; refused with: " << refusal << '\n';
                return false;
            }
            require_disposable_target(row.target, "A11(a)");
            continue;
        }
        if (refusal.empty()) {
            std::cerr << "A11(" << row.label << "): '" << row.target.generic_string()
                      << "' MUST be refused. A gate that passes on the input it exists "
                         "to catch is worse than no gate.\n";
            return false;
        }
        if (refusal.find(row.must_contain) == std::string::npos) {
            std::cerr << "A11(" << row.label << "): the refusal must contain '"
                      << row.must_contain << "'; got: " << refusal << '\n';
            return false;
        }
    }
    std::cout << "MAR-189 A11: the disposable-target predicate accepts a temp path, "
                 "refuses the tracked fixture bundle by name and refuses a path under "
                 "the repository's own build directory.\n";
    return true;
}

// ---------------------------------------------------------------------------
// The byte map (design 6.2).
// ---------------------------------------------------------------------------

using ByteMap = std::map<std::string, std::string>;

void collect_bytes(const std::filesystem::path& root, ByteMap* map) {
    std::error_code error;
    if (!std::filesystem::exists(root, error)) {
        return;
    }
    for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
         iterator != end;
         iterator.increment(error)) {
        if (error) {
            return;
        }
        std::error_code entry_error;
        if (!iterator->is_regular_file(entry_error)) {
            continue;
        }
        map->emplace(
            iterator->path().lexically_normal().generic_string(), mar188::read_all(iterator->path()));
    }
}

/**
 * @brief Every byte of the project directory and the layer directory, keyed by path.
 *
 * A RECURSIVE LISTING, deliberately not an enumerated five-item set. An enumerated
 * set cannot see a file the commit newly created beside the ones it knew about --
 * an orphaned texture under a name nobody predicted, a surviving `.bak`, a
 * `*.tmp.*` left by an interrupted atomic write. "Paths only in after" catches all
 * three for free, and only because nothing here decides in advance what to look at.
 *
 * Whole contents rather than a hash: the bundle is ~20KB, and holding the bytes is
 * what lets a failure name the first differing offset instead of only saying
 * "different".
 */
ByteMap bundle_bytes(const marrow::editor::ProjectData& project) {
    ByteMap map;
    collect_bytes(project.source_path.parent_path(), &map);
    if (project.editor_metadata.import_sources.has_value() &&
        project.editor_metadata.import_sources->psd.has_value()) {
        collect_bytes(
            project.resolve_path(project.editor_metadata.import_sources->psd->layers_directory),
            &map);
    }
    return map;
}

bool expect_bundle_equal(const ByteMap& before, const ByteMap& after, std::string_view label) {
    bool ok = true;
    for (const auto& entry : before) {
        const auto found = after.find(entry.first);
        if (found == after.end()) {
            std::cerr << label << ": only in before: " << entry.first << '\n';
            ok = false;
            continue;
        }
        if (found->second == entry.second) {
            continue;
        }
        ok = false;
        const std::size_t shared = std::min(entry.second.size(), found->second.size());
        std::size_t offset = 0;
        while (offset < shared && entry.second[offset] == found->second[offset]) {
            ++offset;
        }
        std::cerr << label << ": " << entry.first << " differs at offset " << offset;
        if (offset < shared) {
            std::cerr << ": expected 0x" << std::hex << std::setw(2) << std::setfill('0')
                      << static_cast<int>(static_cast<unsigned char>(entry.second[offset]))
                      << ", got 0x" << std::setw(2)
                      << static_cast<int>(static_cast<unsigned char>(found->second[offset]))
                      << std::dec << std::setfill(' ');
        } else {
            std::cerr << ": lengths " << entry.second.size() << " and " << found->second.size();
        }
        std::cerr << '\n';
    }
    for (const auto& entry : after) {
        if (before.find(entry.first) == before.end()) {
            std::cerr << label << ": only in after: " << entry.first << " ("
                      << entry.second.size() << " bytes)\n";
            ok = false;
        }
    }
    return ok;
}

// ---------------------------------------------------------------------------
// The failpoint seams, RAII-scoped.
// ---------------------------------------------------------------------------

struct ScopedCommitFailpoint {
    explicit ScopedCommitFailpoint(marrow::editor::detail::CommitFailpoint callback) {
        marrow::editor::detail::set_psd_commit_failpoint_for_testing(std::move(callback));
    }
    ~ScopedCommitFailpoint() {
        marrow::editor::detail::set_psd_commit_failpoint_for_testing({});
    }
    ScopedCommitFailpoint(const ScopedCommitFailpoint&) = delete;
    ScopedCommitFailpoint& operator=(const ScopedCommitFailpoint&) = delete;
};

struct ScopedRollbackFailpoint {
    explicit ScopedRollbackFailpoint(marrow::editor::detail::CommitRollbackFailpoint callback) {
        marrow::editor::detail::set_psd_commit_rollback_failpoint_for_testing(std::move(callback));
    }
    ~ScopedRollbackFailpoint() {
        marrow::editor::detail::set_psd_commit_rollback_failpoint_for_testing({});
    }
    ScopedRollbackFailpoint(const ScopedRollbackFailpoint&) = delete;
    ScopedRollbackFailpoint& operator=(const ScopedRollbackFailpoint&) = delete;
};

/// @brief Fails after exactly one named step and lets every other step through.
marrow::editor::detail::CommitFailpoint fail_after(marrow::editor::PsdCommitStep step) {
    return [step](marrow::editor::PsdCommitStep reached) -> std::string {
        if (reached != step) {
            return {};
        }
        return std::string("after ") + marrow::editor::psd_commit_step_name(step);
    };
}

// ---------------------------------------------------------------------------
// A disposable project bundle, built by a real import.
// ---------------------------------------------------------------------------

struct Scenario {
    std::filesystem::path directory;
    std::filesystem::path project_path;
    std::filesystem::path candidate_psd;
    std::filesystem::path staging_root;
    marrow::editor::EditorSession session;
    marrow::editor::PsdReimportPlan plan;
};

constexpr const char* kBundleStem = "bundle";

bool open_scenario(
    const std::filesystem::path& scratch,
    const std::string& name,
    const std::vector<mar188::SynthLayer>& initial,
    const std::vector<mar188::SynthLayer>& candidate,
    Scenario* out,
    const std::function<void(marrow::editor::ProjectData*)>& customize = {}) {
    out->directory = scratch / name;
    out->staging_root = scratch / (name + "_staging");
    require_disposable_target(out->directory, name.c_str());
    require_disposable_target(out->staging_root, name.c_str());

    std::error_code error;
    std::filesystem::remove_all(out->directory, error);
    std::filesystem::remove_all(out->staging_root, error);
    error.clear();
    std::filesystem::create_directories(out->directory, error);
    std::filesystem::create_directories(scratch / "psd", error);
    if (error) {
        std::cerr << name << ": scratch directories could not be created: " << error.message()
                  << '\n';
        return false;
    }

    // Both PSDs live OUTSIDE the project directory so the byte map is the bundle
    // and nothing else.
    const std::filesystem::path initial_psd = scratch / "psd" / (name + "_initial.psd");
    out->candidate_psd = scratch / "psd" / (name + "_candidate.psd");
    if (!mar188::write_synthetic_psd(initial_psd, 64, 64, initial) ||
        !mar188::write_synthetic_psd(out->candidate_psd, 64, 64, candidate)) {
        std::cerr << name << ": the synthetic PSDs could not be written.\n";
        return false;
    }

    marrow::editor::PsdImportOptions import_options;
    import_options.psd_path = initial_psd;
    import_options.skeleton_output_path =
        out->directory / (std::string(kBundleStem) + ".mskl");
    import_options.atlas_output_path = out->directory / (std::string(kBundleStem) + ".matl");
    import_options.extracted_layers_directory =
        out->directory / (std::string(kBundleStem) + "_layers");
    import_options.atlas_name = kBundleStem;
    const marrow::editor::PsdImportResult imported =
        marrow::editor::import_psd_to_runtime_bundle(import_options);
    if (!imported) {
        std::cerr << name << ": the initial import failed: " << imported.error->format() << '\n';
        return false;
    }

    // The tracked fixture's `player_idle.matl` declares
    // `"image": "player_fixture.png"` -- the atlas stem and the texture name do
    // NOT agree. An import produces a bundle where they do, so a scenario built
    // straight from one cannot tell "derive the texture from the atlas's stem"
    // apart from "derive it from the atlas document's own `image` member", and
    // every case below would be blind to the defect this story exists to prevent.
    // Renaming the texture and repointing the document reproduces the divergence.
    {
        const std::filesystem::path packed_texture =
            out->directory / (std::string(kBundleStem) + ".png");
        const std::filesystem::path renamed_texture =
            out->directory / (std::string(kBundleStem) + "_tex.png");
        std::error_code rename_error;
        std::filesystem::rename(packed_texture, renamed_texture, rename_error);
        if (rename_error) {
            std::cerr << name << ": the texture could not be renamed: "
                      << rename_error.message() << '\n';
            return false;
        }
        marrow::runtime::json::LoadResult atlas =
            marrow::runtime::json::load_document(import_options.atlas_output_path);
        if (!atlas) {
            std::cerr << name << ": the packed atlas did not parse.\n";
            return false;
        }
        marrow::runtime::json::Value* atlas_object =
            marrow::runtime::json::find_member(atlas.document->root, "atlas");
        if (atlas_object == nullptr || !atlas_object->is_object()) {
            std::cerr << name << ": the packed atlas has no 'atlas' object.\n";
            return false;
        }
        atlas_object->as_object()["image"] =
            make_string_value(renamed_texture.filename().generic_string());
        if (!write_text_file(
                import_options.atlas_output_path,
                marrow::runtime::json::serialize_pretty(atlas.document->root))) {
            std::cerr << name << ": the repointed atlas could not be written.\n";
            return false;
        }
    }

    out->project_path = out->directory / (std::string(kBundleStem) + ".marrow");
    marrow::editor::MinimalProjectOptions project_options;
    project_options.project_path = out->project_path;
    project_options.skeleton_path = import_options.skeleton_output_path;
    project_options.atlas_paths = {import_options.atlas_output_path};
    project_options.name = name;
    project_options.preview_skins = {};
    marrow::editor::ProjectData project =
        marrow::editor::create_minimal_project(project_options);
    marrow::editor::ProjectImportSources sources;
    sources.psd =
        marrow::editor::make_psd_provenance(imported, out->project_path, initial_psd);
    project.editor_metadata.import_sources = std::move(sources);
    if (customize) {
        customize(&project);
    }
    const marrow::editor::ProjectSaveResult saved =
        marrow::editor::save_project(project, out->project_path);
    if (!saved) {
        std::cerr << name << ": the project could not be saved: " << saved.error->format()
                  << '\n';
        return false;
    }
    return true;
}

bool plan_scenario(Scenario* out, const char* label) {
    const marrow::editor::ProjectLoadResult loaded = out->session.open(out->project_path);
    if (!loaded) {
        std::cerr << label << ": the project did not open: " << loaded.error->format() << '\n';
        return false;
    }
    marrow::editor::PsdReimportPlanOptions plan_options;
    plan_options.psd_path = out->candidate_psd;
    plan_options.staging_root = out->staging_root;
    // The TARGET's own names. This is what makes placement a byte copy whose
    // result still resolves.
    plan_options.staged_skeleton_filename =
        out->session.project()->resolved_skeleton_path().filename().generic_string();
    // From the CURRENT TEXTURE's stem, not the atlas's. The packer writes the
    // atlas document's `image` from the staged atlas path, so this is what makes
    // the committed `image` still name the file that is actually beside it.
    const std::filesystem::path target_atlas =
        out->session.project()->resolved_atlas_paths().front();
    plan_options.staged_atlas_filename =
        std::filesystem::path(atlas_member(target_atlas, "image")).stem().generic_string() +
        ".matl";
    out->plan = marrow::editor::plan_psd_reimport(*out->session.project(), plan_options);
    if (!out->plan) {
        std::cerr << label << ": the reimport plan failed: " << out->plan.error->format() << '\n';
        return false;
    }
    return true;
}

std::vector<std::string> step_names(const std::vector<marrow::editor::PsdCommitStep>& steps) {
    std::vector<std::string> names;
    names.reserve(steps.size());
    for (const marrow::editor::PsdCommitStep step : steps) {
        names.emplace_back(marrow::editor::psd_commit_step_name(step));
    }
    return names;
}

bool expect_steps(
    const std::vector<std::string>& actual,
    const std::vector<std::string>& expected,
    std::string_view label) {
    if (actual == expected) {
        return true;
    }
    std::cerr << label << ": step ledger mismatch.\n  expected:";
    for (const std::string& name : expected) {
        std::cerr << ' ' << name;
    }
    std::cerr << "\n  actual  :";
    for (const std::string& name : actual) {
        std::cerr << ' ' << name;
    }
    std::cerr << '\n';
    for (const std::string& name : expected) {
        if (std::find(actual.begin(), actual.end(), name) == actual.end()) {
            std::cerr << "  missing: " << name << '\n';
        }
    }
    for (const std::string& name : actual) {
        if (std::find(expected.begin(), expected.end(), name) == expected.end()) {
            std::cerr << "  unexpected: " << name << '\n';
        }
    }
    return false;
}

/// @brief Every `*.marrow-journal*` and `*.tmp.*` anywhere in the bundle's directories.
std::vector<std::string> journal_residue_scan(const marrow::editor::ProjectData& project) {
    std::vector<std::string> found;
    ByteMap map;
    collect_bytes(project.source_path.parent_path(), &map);
    if (project.editor_metadata.import_sources.has_value() &&
        project.editor_metadata.import_sources->psd.has_value()) {
        collect_bytes(
            project.resolve_path(project.editor_metadata.import_sources->psd->layers_directory),
            &map);
    }
    for (const auto& entry : map) {
        const std::string name = std::filesystem::path(entry.first).filename().string();
        if (name.find(".marrow-journal") != std::string::npos ||
            name.find(".tmp.") != std::string::npos) {
            found.push_back(entry.first);
        }
    }
    return found;
}

/**
 * @brief Adds a skin holding a mesh attachment, optionally with a deform timeline.
 *
 * The attachment is named something NO SLOT names, deliberately. With `skins`
 * absent the parser synthesises a default skin from the slots' own `attachment`
 * members (`skeleton_parse.cpp:4933-4935`), so a mesh named after the slot's
 * attachment would still resolve and the refusal would change character.
 *
 * @param skeleton_path Skeleton document to patch in place.
 * @param with_deform When true, also adds an animation whose deform timeline
 *        targets the mesh -- which is what makes the skin's loss OBSERVABLE to
 *        `build_project_runtime`. When false, nothing references the skin by
 *        name, which is the case R2(d) measures.
 */
bool add_shadow_mesh_skin(const std::filesystem::path& skeleton_path, bool with_deform) {
    marrow::runtime::json::LoadResult loaded =
        marrow::runtime::json::load_document(skeleton_path);
    if (!loaded) {
        std::cerr << "add_shadow_mesh_skin: the bundle skeleton did not parse.\n";
        return false;
    }
    marrow::runtime::json::Value::Object& root = loaded.document->root.as_object();

    marrow::runtime::json::Value::Object mesh;
    mesh.emplace("attachment", make_string_value("shadow_mesh"));
    mesh.emplace("type", make_string_value("mesh"));
    mesh.emplace("region", make_string_value("shadow"));
    mesh.emplace(
        "vertices",
        make_array_value({make_number_value(-8.0), make_number_value(-4.0),
                          make_number_value(8.0), make_number_value(-4.0),
                          make_number_value(8.0), make_number_value(4.0),
                          make_number_value(-8.0), make_number_value(4.0)}));
    mesh.emplace(
        "triangles",
        make_array_value({make_number_value(0.0), make_number_value(1.0),
                          make_number_value(2.0), make_number_value(2.0),
                          make_number_value(3.0), make_number_value(0.0)}));
    mesh.emplace(
        "uvs",
        make_array_value({make_number_value(0.0), make_number_value(0.0),
                          make_number_value(1.0), make_number_value(0.0),
                          make_number_value(1.0), make_number_value(1.0),
                          make_number_value(0.0), make_number_value(1.0)}));
    marrow::runtime::json::Value::Array weights;
    for (const auto& corner : std::vector<std::pair<double, double>>{
             {-8.0, -4.0}, {8.0, -4.0}, {8.0, 4.0}, {-8.0, 4.0}}) {
        marrow::runtime::json::Value::Object bind;
        bind.emplace("bone", make_string_value("root"));
        bind.emplace("x", make_number_value(corner.first));
        bind.emplace("y", make_number_value(corner.second));
        bind.emplace("weight", make_number_value(1.0));
        weights.push_back(make_array_value({make_object_value(std::move(bind))}));
    }
    mesh.emplace("weights", make_array_value(std::move(weights)));

    // `skins.<skin>.<slot>` IS the attachment object, named by its own
    // `attachment` member -- one per slot per skin, not a map of names.
    marrow::runtime::json::Value::Object default_skin;
    default_skin.emplace("shadow", make_object_value(std::move(mesh)));
    marrow::runtime::json::Value::Object skins;
    skins.emplace("default", make_object_value(std::move(default_skin)));
    root["skins"] = make_object_value(std::move(skins));

    if (with_deform) {
        marrow::runtime::json::Value::Array keys;
        for (const double time : {0.0, 0.5}) {
            marrow::runtime::json::Value::Object key;
            key.emplace("time", make_number_value(time));
            key.emplace(
                "vertices",
                make_array_value({make_number_value(0.0), make_number_value(0.0),
                                  make_number_value(0.0), make_number_value(0.0),
                                  make_number_value(0.0), make_number_value(0.0),
                                  make_number_value(0.0), make_number_value(0.0)}));
            key.emplace("curve", make_string_value("linear"));
            keys.push_back(make_object_value(std::move(key)));
        }
        marrow::runtime::json::Value::Object attachment_deform;
        attachment_deform.emplace("shadow_mesh", make_array_value(std::move(keys)));
        marrow::runtime::json::Value::Object slot_deform;
        slot_deform.emplace("shadow", make_object_value(std::move(attachment_deform)));
        marrow::runtime::json::Value::Object deform_holder;
        deform_holder.emplace("deform", make_object_value(std::move(slot_deform)));
        marrow::runtime::json::Value::Object animations;
        animations.emplace("idle", make_object_value(std::move(deform_holder)));
        root["animations"] = make_object_value(std::move(animations));
    }

    if (!write_text_file(
            skeleton_path, marrow::runtime::json::serialize_pretty(loaded.document->root))) {
        std::cerr << "add_shadow_mesh_skin: the patched skeleton could not be written.\n";
        return false;
    }
    return true;
}

/**
 * @brief Plants `skins.default.<slot>` as a mesh attachment, and optionally the slot.
 *
 * @param region Atlas region the mesh points at -- must exist in the atlas that
 *        document will be validated against, or the runtime build fails for a
 *        reason that has nothing to do with the case using this.
 * @param with_slot Also append `<slot>` to `$.slots`, which a staged document
 *        needs before the prune has anything to remove.
 */
bool plant_skin_attachment(
    const std::filesystem::path& skeleton_path,
    const std::string& slot,
    const std::string& attachment,
    const std::string& region,
    bool with_slot) {
    marrow::runtime::json::LoadResult loaded =
        marrow::runtime::json::load_document(skeleton_path);
    if (!loaded) {
        std::cerr << "plant_skin_attachment: " << skeleton_path.generic_string()
                  << " did not parse.\n";
        return false;
    }
    marrow::runtime::json::Value::Object& root = loaded.document->root.as_object();

    marrow::runtime::json::Value::Object mesh;
    mesh.emplace("attachment", make_string_value(attachment));
    mesh.emplace("type", make_string_value("mesh"));
    mesh.emplace("region", make_string_value(region));
    mesh.emplace(
        "vertices",
        make_array_value({make_number_value(-8.0), make_number_value(-4.0),
                          make_number_value(8.0), make_number_value(-4.0),
                          make_number_value(8.0), make_number_value(4.0),
                          make_number_value(-8.0), make_number_value(4.0)}));
    mesh.emplace(
        "triangles",
        make_array_value({make_number_value(0.0), make_number_value(1.0),
                          make_number_value(2.0), make_number_value(2.0),
                          make_number_value(3.0), make_number_value(0.0)}));
    mesh.emplace(
        "uvs",
        make_array_value({make_number_value(0.0), make_number_value(0.0),
                          make_number_value(1.0), make_number_value(0.0),
                          make_number_value(1.0), make_number_value(1.0),
                          make_number_value(0.0), make_number_value(1.0)}));
    marrow::runtime::json::Value::Array weights;
    for (int vertex = 0; vertex < 4; ++vertex) {
        marrow::runtime::json::Value::Object bind;
        bind.emplace("bone", make_string_value("root"));
        bind.emplace("x", make_number_value(0.0));
        bind.emplace("y", make_number_value(0.0));
        bind.emplace("weight", make_number_value(1.0));
        weights.push_back(make_array_value({make_object_value(std::move(bind))}));
    }
    mesh.emplace("weights", make_array_value(std::move(weights)));

    marrow::runtime::json::Value* existing =
        marrow::runtime::json::find_member(loaded.document->root, "skins");
    marrow::runtime::json::Value::Object skins =
        existing != nullptr && existing->is_object()
        ? existing->as_object()
        : marrow::runtime::json::Value::Object{};
    marrow::runtime::json::Value::Object default_skin;
    if (const auto found = skins.find("default");
        found != skins.end() && found->second.is_object()) {
        default_skin = found->second.as_object();
    }
    default_skin[slot] = make_object_value(std::move(mesh));
    skins["default"] = make_object_value(std::move(default_skin));
    root["skins"] = make_object_value(std::move(skins));

    if (with_slot) {
        marrow::runtime::json::Value* slots =
            marrow::runtime::json::find_member(loaded.document->root, "slots");
        if (slots == nullptr || !slots->is_array()) {
            std::cerr << "plant_skin_attachment: no slots array.\n";
            return false;
        }
        marrow::runtime::json::Value::Object entry;
        entry.emplace("name", make_string_value(slot));
        entry.emplace("bone", make_string_value("root"));
        entry.emplace("attachment", make_string_value(attachment));
        slots->as_array().push_back(make_object_value(std::move(entry)));
    }
    return write_text_file(
        skeleton_path, marrow::runtime::json::serialize_pretty(loaded.document->root));
}

std::string atlas_image_of(const std::filesystem::path& atlas_path) {
    return atlas_member(atlas_path, "image");
}

}  // namespace mar189

/**
 * @brief R0-R8 -- the commit, its rollback, and the bytes both leave behind.
 *
 * Run in this order; every attribution in the inversion register is by RUN order.
 * A11's self-test runs first and aborts the process rather than let any case here
 * write into the repository.
 */
bool validate_mar189_reimport_commit(const std::filesystem::path& scratch) {
    using marrow::editor::PsdCommitStep;
    using mar189::ByteMap;
    using mar189::Scenario;

    if (!mar189::run_disposable_target_self_test()) {
        return false;
    }

    std::error_code directory_error;
    mar189::require_disposable_target(scratch, "R-suite");
    std::filesystem::remove_all(scratch, directory_error);
    directory_error.clear();
    std::filesystem::create_directories(scratch, directory_error);
    if (directory_error) {
        std::cerr << "R-suite: scratch could not be created: " << directory_error.message()
                  << '\n';
        return false;
    }

    const std::vector<mar188::SynthLayer> initial_tree = mar188::fixture_tree();
    // The candidate keeps the grouped layers and drops `shadow`, so a reimport is
    // an Updated pair plus one Missing -- which is what exercises `preserve`.
    const std::vector<mar188::SynthLayer> candidate_tree = {
        {{"torso"}, "arm_l", 4, 20, 12, 8, 41U, 51U, 61U},
        {{"torso"}, "body", 16, 12, 20, 24, 71U, 81U, 91U},
    };

    const std::vector<std::string> all_step_names = mar189::step_names(
        std::vector<PsdCommitStep>(
            marrow::editor::kAllCommitSteps.begin(), marrow::editor::kAllCommitSteps.end()));

    // ---- R0 -- the step ledger is the enum, in order -------------------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r0", initial_tree, candidate_tree, &scenario) ||
            !mar189::plan_scenario(&scenario, "R0")) {
            return false;
        }
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, scenario.plan, options);
        if (!result) {
            std::cerr << "R0: a clean commit must succeed; got '" << result.error << "'.\n";
            return false;
        }
        if (!mar189::expect_steps(
                mar189::step_names(result.steps_executed), all_step_names, "R0")) {
            return false;
        }
        if (!result.error.empty() || result.rolled_back || !result.journal_residue.empty() ||
            !result.steps_rolled_back.empty()) {
            std::cerr << "R0: a clean commit must report no error, no rollback and no "
                         "residue; error='"
                      << result.error << "' rolled_back=" << result.rolled_back
                      << " residue=" << result.journal_residue.size()
                      << " rolled_back_steps=" << result.steps_rolled_back.size() << '\n';
            return false;
        }
    }

    // ---- R1 -- success replaces the bundle ----------------------------------
    ByteMap committed_bytes;
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r1", initial_tree, candidate_tree, &scenario) ||
            !mar189::plan_scenario(&scenario, "R1")) {
            return false;
        }
        const std::uint64_t revision_before = scenario.session.runtime_revision();
        const std::filesystem::path target_skeleton =
            scenario.session.project()->resolved_skeleton_path();
        const std::filesystem::path target_atlas =
            scenario.session.project()->resolved_atlas_paths().front();
        const std::filesystem::path target_texture =
            target_atlas.parent_path() / mar189::atlas_image_of(target_atlas);
        const std::filesystem::path target_layers = scenario.session.project()->resolve_path(
            scenario.session.project()->editor_metadata.import_sources->psd->layers_directory);
        const std::string staged_skeleton = mar188::read_all(scenario.plan.staged_skeleton_path);
        const std::string staged_atlas = mar188::read_all(scenario.plan.staged_atlas_path);
        const std::string staged_texture = mar188::read_all(scenario.plan.staged_texture_path);
        const std::vector<std::string> staged_layers =
            mar188::directory_listing(scenario.plan.staged_layers_directory);

        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, scenario.plan, options);
        if (!result) {
            std::cerr << "R1: the commit must succeed; got '" << result.error << "'.\n";
            return false;
        }
        const std::vector<std::pair<const char*, std::pair<std::filesystem::path, std::string>>>
            placed = {
                {"skeleton", {target_skeleton, staged_skeleton}},
                {"atlas", {target_atlas, staged_atlas}},
                {"texture", {target_texture, staged_texture}},
            };
        for (const auto& entry : placed) {
            const std::string actual = mar188::read_all(entry.second.first);
            if (actual != entry.second.second) {
                std::cerr << "R1: the committed " << entry.first << " ('"
                          << entry.second.first.generic_string()
                          << "') must equal the staged bytes; sizes " << actual.size()
                          << " and " << entry.second.second.size() << ".\n";
                return false;
            }
        }
        std::vector<std::string> committed_layers = mar188::directory_listing(target_layers);
        std::vector<std::string> expected_layers;
        for (const std::string& row : staged_layers) {
            const std::size_t split = row.rfind(" (");
            expected_layers.push_back(
                std::filesystem::path(row.substr(0, split)).filename().generic_string() +
                row.substr(split));
        }
        std::vector<std::string> actual_layers;
        for (const std::string& row : committed_layers) {
            const std::size_t split = row.rfind(" (");
            actual_layers.push_back(
                std::filesystem::path(row.substr(0, split)).filename().generic_string() +
                row.substr(split));
        }
        std::sort(expected_layers.begin(), expected_layers.end());
        std::sort(actual_layers.begin(), actual_layers.end());
        if (actual_layers != expected_layers) {
            std::cerr << "R1: the committed layer directory must equal the staged one.\n";
            for (const std::string& row : expected_layers) {
                if (std::find(actual_layers.begin(), actual_layers.end(), row) ==
                    actual_layers.end()) {
                    std::cerr << "  missing: " << row << '\n';
                }
            }
            for (const std::string& row : actual_layers) {
                if (std::find(expected_layers.begin(), expected_layers.end(), row) ==
                    expected_layers.end()) {
                    std::cerr << "  unexpected: " << row << '\n';
                }
            }
            return false;
        }
        const marrow::editor::ProjectLoadResult reloaded =
            marrow::editor::load_project(scenario.project_path);
        if (!reloaded) {
            std::cerr << "R1: the committed project must load; got "
                      << reloaded.error->format() << '\n';
            return false;
        }
        const marrow::runtime::json::LoadResult reloaded_document =
            marrow::runtime::load_skeleton_document(reloaded.project->resolved_skeleton_path());
        if (!reloaded_document) {
            std::cerr << "R1: the committed skeleton must load; got "
                      << reloaded_document.error->format() << '\n';
            return false;
        }
        const marrow::editor::ProjectRuntimeResult rebuilt =
            marrow::editor::build_project_runtime(*reloaded.project, *reloaded_document.document);
        if (!rebuilt) {
            std::cerr << "R1: the committed bundle must build a runtime; got "
                      << rebuilt.error->message << '\n';
            return false;
        }
        const marrow::runtime::AtlasDataResult atlas =
            marrow::runtime::AtlasLoader::load(target_atlas);
        if (!atlas) {
            std::cerr << "R1: the committed atlas must load; got " << atlas.error->format()
                      << '\n';
            return false;
        }
        // A successful commit ADOPTS, and adoption bumps the revision. Asserting it
        // unmoved here would be a gate that fails on correct code.
        if (scenario.session.runtime_revision() == revision_before) {
            std::cerr << "R1: a successful commit must move runtime_revision(); it stayed at "
                      << revision_before << ".\n";
            return false;
        }
        committed_bytes = mar189::bundle_bytes(*scenario.session.project());
    }

    // ---- R1b -- the committed atlas still names the texture beside it -------
    //
    // The invariant is a comparison against state captured BEFORE the commit. Every
    // clause phrased against the committed bundle alone is self-satisfying: "that
    // file exists beside the atlas" is satisfied because `PlaceTexture` created it,
    // and "the target png's file name" is circular because the staging rule is what
    // defines the target png.
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r1b", initial_tree, candidate_tree, &scenario) ||
            !mar189::plan_scenario(&scenario, "R1b")) {
            return false;
        }
        const std::filesystem::path target_atlas =
            scenario.session.project()->resolved_atlas_paths().front();
        const std::string image_before = mar189::atlas_image_of(target_atlas);
        const ByteMap before = mar189::bundle_bytes(*scenario.session.project());

        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, scenario.plan, options);
        if (!result) {
            std::cerr << "R1b: the commit must succeed; got '" << result.error << "'.\n";
            return false;
        }
        const std::string image_after = mar189::atlas_image_of(target_atlas);
        if (image_after != image_before) {
            std::cerr << "R1b: the committed atlas references image '" << image_after
                      << "'; the pre-commit atlas referenced '" << image_before
                      << "'. A byte-perfect copy of a document whose bytes encode a "
                         "path is byte-perfect and broken.\n";
            return false;
        }
        // The path SET outside the layer directory must be unchanged. This is what
        // sees an orphan: a texture placed under a name the atlas no longer uses
        // appears here, and the one it stopped using vanishes here, while every
        // byte clause in R1 passes. The layer directory is excluded because its
        // membership legitimately changes -- a dropped layer's PNG is meant to go.
        const ByteMap after = mar189::bundle_bytes(*scenario.session.project());
        const std::string layers_prefix =
            scenario.session.project()
                ->resolve_path(scenario.session.project()
                                   ->editor_metadata.import_sources->psd->layers_directory)
                .lexically_normal()
                .generic_string();
        const auto outside_layers = [&layers_prefix](const std::string& path) {
            return path.rfind(layers_prefix, 0) != 0U;
        };
        std::vector<std::string> only_before;
        std::vector<std::string> only_after;
        for (const auto& entry : before) {
            if (outside_layers(entry.first) && after.find(entry.first) == after.end()) {
                only_before.push_back(entry.first);
            }
        }
        for (const auto& entry : after) {
            if (outside_layers(entry.first) && before.find(entry.first) == before.end()) {
                only_after.push_back(entry.first);
            }
        }
        if (!only_before.empty() || !only_after.empty()) {
            std::cerr << "R1b: a commit must replace the bundle's files, never add or "
                         "orphan one.\n";
            for (const std::string& row : only_before) {
                std::cerr << "  vanished: " << row << '\n';
            }
            for (const std::string& row : only_after) {
                std::cerr << "  appeared: " << row << '\n';
            }
            return false;
        }
        const std::string texture_bytes =
            mar188::read_all(target_atlas.parent_path() / image_after);
        if (texture_bytes.empty() ||
            texture_bytes != mar188::read_all(scenario.plan.staged_texture_path)) {
            std::cerr << "R1b: the file the committed atlas names ('" << image_after
                      << "') must hold the staged texture's bytes; it holds "
                      << texture_bytes.size() << ".\n";
            return false;
        }
    }

    // ---- R1c -- a bundle staged under the wrong name is REFUSED -------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r1c", initial_tree, candidate_tree, &scenario)) {
            return false;
        }
        const marrow::editor::ProjectLoadResult loaded =
            scenario.session.open(scenario.project_path);
        if (!loaded) {
            std::cerr << "R1c: the project did not open: " << loaded.error->format() << '\n';
            return false;
        }
        marrow::editor::PsdReimportPlanOptions plan_options;
        plan_options.psd_path = scenario.candidate_psd;
        plan_options.staging_root = scenario.staging_root;
        // MAR-188's defaults, which is exactly what reverting the naming rule
        // produces.
        const marrow::editor::PsdReimportPlan plan =
            marrow::editor::plan_psd_reimport(*scenario.session.project(), plan_options);
        if (!plan) {
            std::cerr << "R1c: the default-named plan must succeed; got "
                      << plan.error->format() << '\n';
            return false;
        }
        const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, plan, options);
        const std::string expected =
            "ValidateRequest: the staged atlas references image 'staged.png' but the "
            "project atlas 'bundle.matl' references 'bundle_tex.png'; committing it "
            "would name a texture that is not beside it";
        if (result || result.error != expected) {
            std::cerr << "R1c: a staged bundle naming a different texture must be refused "
                         "with '"
                      << expected << "'; got ok=" << result.ok << " error='" << result.error
                      << "'.\n";
            return false;
        }
        if (!mar189::expect_bundle_equal(
                before, mar189::bundle_bytes(*scenario.session.project()), "R1c")) {
            return false;
        }
    }

    // ---- R2(a) -- a parse failure never reaches the committer. WITNESS. ----
    //
    // Green before MAR-189 existed: this is MAR-188's planner behaviour, and no
    // MAR-189 inversion reddens it. Kept because "a corrupt candidate changes
    // nothing" is a real claim, and labelled rather than counted as coverage.
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r2a", initial_tree, candidate_tree, &scenario)) {
            return false;
        }
        const marrow::editor::ProjectLoadResult loaded =
            scenario.session.open(scenario.project_path);
        if (!loaded) {
            std::cerr << "R2(a): the project did not open: " << loaded.error->format() << '\n';
            return false;
        }
        const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
        const std::uint64_t revision_before = scenario.session.runtime_revision();
        const std::filesystem::path corrupt = scratch / "psd" / "r2a_corrupt.psd";
        if (!write_text_file(corrupt, "8BPSnot-a-psd")) {
            std::cerr << "R2(a): the corrupt candidate could not be written.\n";
            return false;
        }
        marrow::editor::PsdReimportPlanOptions plan_options;
        plan_options.psd_path = corrupt;
        plan_options.staging_root = scratch / "r2a_staging";
        plan_options.staged_skeleton_filename = std::string(mar189::kBundleStem) + ".mskl";
        plan_options.staged_atlas_filename = std::string(mar189::kBundleStem) + ".matl";
        const marrow::editor::PsdReimportPlan plan =
            marrow::editor::plan_psd_reimport(*scenario.session.project(), plan_options);
        if (plan) {
            std::cerr << "R2(a): a corrupt PSD must not produce a plan.\n";
            return false;
        }
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, plan, options);
        if (result || result.error.rfind("ValidateRequest: the plan carries an error", 0) != 0) {
            std::cerr << "R2(a): committing an errored plan must be refused at "
                         "ValidateRequest; got ok="
                      << result.ok << " error='" << result.error << "'.\n";
            return false;
        }
        if (!mar189::expect_bundle_equal(
                before, mar189::bundle_bytes(*scenario.session.project()), "R2(a)")) {
            return false;
        }
        if (scenario.session.runtime_revision() != revision_before) {
            std::cerr << "R2(a): runtime_revision() must be unmoved.\n";
            return false;
        }
    }

    // ---- R2(b) -- the overlay refuses the staged bundle, before any target moves
    {
        Scenario scenario;
        // A candidate with NO groups produces only the `root` bone, so `torso`
        // -- which the initial import's group produced -- stops existing.
        const std::vector<mar188::SynthLayer> ungrouped = {
            {{}, "shadow", 18, 40, 14, 8, 10U, 20U, 30U},
            {{}, "arm_l", 4, 20, 12, 8, 40U, 50U, 60U},
        };
        if (!mar189::open_scenario(
                scratch, "r2b", initial_tree, ungrouped, &scenario,
                [](marrow::editor::ProjectData* project) {
                    // `torso` is a bone the initial PSD's group produces and the
                    // candidate does not. The staged bundle replaces bones
                    // WHOLESALE (`psd_import.cpp` rebuilds `bones` and `slots`),
                    // so this constraint stops resolving.
                    marrow::editor::IkConstraintEdit edit;
                    edit.name = "overlay_probe";
                    edit.bone_names = {"torso"};
                    edit.target_bone_name = "root";
                    project->ik_constraint_edits.push_back(std::move(edit));
                }) ||
            !mar189::plan_scenario(&scenario, "R2(b)")) {
            return false;
        }
        const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
        const std::uint64_t revision_before = scenario.session.runtime_revision();
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, scenario.plan, options);
        if (result) {
            std::cerr << "R2(b): a staged bundle that drops a bone the project's overlay "
                         "names must be refused before any target changes; the commit "
                         "reported success.\n";
            return false;
        }
        if (result.error.rfind("ValidateStagedBundle:", 0) != 0) {
            std::cerr << "R2(b): the refusal must name ValidateStagedBundle; got '"
                      << result.error << "'.\n";
            return false;
        }
        if (!result.failed_step.has_value() ||
            *result.failed_step != PsdCommitStep::ValidateStagedBundle) {
            std::cerr << "R2(b): failed_step must be ValidateStagedBundle.\n";
            return false;
        }
        // The failing step is NOT in the ledger: `advance` runs only after a step's
        // body succeeds. This is the clause that sees an append moved before the body.
        if (std::find(
                result.steps_executed.begin(),
                result.steps_executed.end(),
                PsdCommitStep::ValidateStagedBundle) != result.steps_executed.end()) {
            std::cerr << "R2(b): ValidateStagedBundle must not appear in steps_executed "
                         "when it failed.\n";
            return false;
        }
        if (!mar189::expect_bundle_equal(
                before, mar189::bundle_bytes(*scenario.session.project()), "R2(b)")) {
            return false;
        }
        if (scenario.session.runtime_revision() != revision_before) {
            std::cerr << "R2(b): runtime_revision() must be unmoved.\n";
            return false;
        }
        std::cout << "R2(b): refused with -- " << result.error << '\n';
    }

    // ---- R2(c) -- the SKINS LOSS refuses on its own message ------------------
    //
    // R2(b) refuses on `$.ik[0].bones[0]: ik constraint references unknown bone`,
    // which is a consequence of the BONES replacement. `build_skeleton_document`
    // also calls `root->erase("skins")` (`psd_import.cpp:1041`), erasing every
    // skin and attachment definition -- and nothing above proves that half is
    // caught, because the IK check trips first.
    //
    // This case makes the skins loss the ONLY thing wrong. It keeps every bone
    // the candidate produces, so no bone reference can fail, and adds a skin
    // holding a MESH attachment plus a deform timeline that targets it. The
    // importer preserves `animations` verbatim while erasing `skins`, so the
    // timeline survives into the staged document and its target does not. With
    // skins absent the parser synthesises a default skin from the slots' own
    // `attachment` names, which is why the mesh is named something no slot names.
    {
        Scenario scenario;
        // The candidate is the SAME tree as the initial import, so it produces the
        // identical bone and slot sets. Nothing bone-shaped and nothing
        // slot-shaped can be what refuses, which is what leaves the erased skin
        // as the only difference.
        if (!mar189::open_scenario(scratch, "r2c", initial_tree, initial_tree, &scenario)) {
            return false;
        }
        const std::filesystem::path skeleton_path =
            scenario.directory / (std::string(mar189::kBundleStem) + ".mskl");
        if (!mar189::add_shadow_mesh_skin(skeleton_path, true)) {
            return false;
        }
        if (!mar189::plan_scenario(&scenario, "R2(c)")) {
            return false;
        }
        const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, scenario.plan, options);
        if (result) {
            std::cerr << "R2(c): a staged bundle that erases the skin holding a deform "
                         "target must be refused; the commit reported success.\n";
            return false;
        }
        const std::string expected =
            "ValidateStagedBundle: the staged bundle does not build with the project's "
            "overlays: $.animations.idle.deform.shadow.shadow_mesh: deform timeline "
            "references unknown attachment 'shadow_mesh'";
        if (result.error != expected) {
            std::cerr << "R2(c): the refusal must name the lost ATTACHMENT, not a bone. "
                         "Expected '"
                      << expected << "'; got '" << result.error << "'.\n";
            return false;
        }
        if (!mar189::expect_bundle_equal(
                before, mar189::bundle_bytes(*scenario.session.project()), "R2(c)")) {
            return false;
        }
        std::cout << "R2(c): refused with -- " << result.error << '\n';
    }

    // ---- R2(d) -- the skins loss is refused STRUCTURALLY ----------------------
    //
    // This case has a history worth keeping, because the transition is the best
    // evidence in the story. It was first written as a CHARACTERIZATION case: it
    // asserted that this project -- hand-authored skins and a mesh-weight overlay,
    // no IK constraint and no deform timeline -- was COMMITTED, and that the
    // committed skeleton had no `skins` member. That was not a hypothetical. It
    // was measured, and it meant the commit path destroyed a user's rig and
    // reported success.
    //
    // R2(c) refuses because a deform timeline NAMES the lost attachment, so the
    // runtime build has something to fail on. Nothing here names it, which is
    // exactly why a runtime-build check could not see it and a STRUCTURAL
    // comparison had to be added: the staged skeleton's `<skin>/<slot>` set is
    // compared against the current skeleton's before any target moves.
    //
    // The case now asserts the refusal. Its predecessor is the reason the refusal
    // exists.
    {
        Scenario scenario;
        if (!mar189::open_scenario(
                scratch, "r2d", initial_tree, initial_tree, &scenario,
                [](marrow::editor::ProjectData* project) {
                    marrow::editor::MeshWeightAttachmentEdit weights;
                    weights.skin_name = "default";
                    weights.slot_name = "shadow";
                    weights.attachment_name = "shadow_mesh";
                    for (int vertex = 0; vertex < 4; ++vertex) {
                        marrow::editor::MeshWeightInfluenceEdit influence;
                        influence.bone_name = "root";
                        influence.weight = 1.0;
                        marrow::editor::MeshWeightVertexEdit edit;
                        edit.influences.push_back(influence);
                        weights.vertices.push_back(std::move(edit));
                    }
                    project->mesh_weight_attachment_edits.push_back(std::move(weights));
                })) {
            return false;
        }
        const std::filesystem::path skeleton_path =
            scenario.directory / (std::string(mar189::kBundleStem) + ".mskl");
        // No deform timeline. Nothing in the runtime document references the skin
        // by name, which is the whole point of this case.
        if (!mar189::add_shadow_mesh_skin(skeleton_path, false)) {
            return false;
        }
        if (!mar189::plan_scenario(&scenario, "R2(d)")) {
            return false;
        }
        const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, scenario.plan, options);

        const std::string expected =
            "ValidateStagedBundle: the staged skeleton drops skin attachments the "
            "project's skeleton defines (default/shadow); a PSD reimport replaces "
            "bones, slots and skins wholesale, so committing it would destroy "
            "hand-authored attachments. There is no override in this version. If "
            "the affected layers are GONE from the PSD, mark them for deletion in "
            "the reimport plan -- that is not destructive and it tells the commit "
            "the loss is intended. If they are still IN the PSD there is no "
            "non-destructive route: removing the attachments from the project's "
            "skeleton by hand DESTROYS the same authored data this refusal is "
            "protecting, is undoable only through the editor's undo, and should be "
            "preceded by a backup";
        if (result) {
            std::cerr << "R2(d): a staged bundle that erases a hand-authored skin must "
                         "be refused even when NOTHING references it; the commit "
                         "reported success.\n";
            return false;
        }
        if (result.error != expected) {
            std::cerr << "R2(d): expected '" << expected << "'; got '" << result.error
                      << "'.\n";
            return false;
        }
        // The identity is named, not counted. A count would not say WHICH
        // attachment was about to be destroyed, which is the only part of the
        // message a user can act on.
        if (result.error.find("default/shadow") == std::string::npos) {
            std::cerr << "R2(d): the refusal must name the lost attachment identity.\n";
            return false;
        }
        if (!mar189::expect_bundle_equal(
                before, mar189::bundle_bytes(*scenario.session.project()), "R2(d)")) {
            return false;
        }
        std::cout << "R2(d): refused with -- " << result.error << '\n';
    }

    std::cout << "MAR-189 R0-R2: a clean commit walks the whole step enum in order and "
                 "replaces skeleton, atlas, texture and layers with the staged bytes; the "
                 "committed atlas still names the texture the PRE-COMMIT atlas named and "
                 "the bundle gains and loses no file; a bundle staged under a different "
                 "texture name is refused by message; and a corrupt candidate and an "
                 "overlay-incompatible candidate each leave every byte untouched.\n";
    return true;
}

namespace mar189 {

/** @brief Slot names of a skeleton document, in document order. */
std::vector<std::string> slot_names(const marrow::runtime::json::Document& document) {
    std::vector<std::string> names;
    const marrow::runtime::json::Value* slots =
        marrow::runtime::json::find_member(document.root, "slots");
    if (slots == nullptr || !slots->is_array()) {
        return names;
    }
    for (const marrow::runtime::json::Value& slot : slots->as_array()) {
        const marrow::runtime::json::Value* name =
            marrow::runtime::json::find_member(slot, "name");
        names.push_back(name != nullptr && name->is_string() ? name->as_string() : "<unnamed>");
    }
    return names;
}

/** @brief The session's active runtime source, as text, for an unchanged-ness clause. */
std::string active_skeleton_source(const marrow::editor::EditorSession& session) {
    const marrow::runtime::json::Document* document = session.base_skeleton_document();
    if (document == nullptr) {
        return "<none>";
    }
    return marrow::runtime::json::serialize_pretty(document->root);
}

/** @brief The stored provenance as an ordered full-tuple row list. */
std::vector<std::string> provenance_rows(const marrow::editor::ProjectData& project) {
    std::vector<std::string> rows;
    if (!project.editor_metadata.import_sources.has_value() ||
        !project.editor_metadata.import_sources->psd.has_value()) {
        return rows;
    }
    const marrow::editor::PsdImportProvenance& psd =
        *project.editor_metadata.import_sources->psd;
    rows.push_back("source=" + psd.source_path.generic_string());
    rows.push_back("layers=" + psd.layers_directory.generic_string());
    for (const marrow::editor::PsdLayerProvenance& layer : psd.layers) {
        std::string identity;
        for (const std::string& segment : layer.group_path) {
            identity += segment + "|";
        }
        identity += layer.layer_name;
        rows.push_back(
            identity + " slot=" + layer.slot_name + " attachment=" + layer.attachment_name +
            " bone=" + layer.bone_name + " image=" + layer.image_file);
    }
    return rows;
}

bool expect_rows_equal(
    const std::vector<std::string>& actual,
    const std::vector<std::string>& expected,
    std::string_view label) {
    if (actual == expected) {
        return true;
    }
    std::cerr << label << ": row list mismatch.\n";
    for (std::size_t index = 0; index < std::max(actual.size(), expected.size()); ++index) {
        const std::string left = index < expected.size() ? expected[index] : "<none>";
        const std::string right = index < actual.size() ? actual[index] : "<none>";
        if (left != right) {
            std::cerr << "  index " << index << ": expected '" << left << "', got '" << right
                      << "'\n";
        }
    }
    return false;
}

/**
 * @brief Every overlay vector this story must not disturb, one line per element.
 *
 * Built and compared IN MEMORY. A `.marrow` round trip normalises object order
 * (`Value::Object` is a `std::map`) and is not bit-exact for a 17-significant-digit
 * double, so a case that compares through a file is asserting on a different value
 * than it wrote.
 */
std::vector<std::string> overlay_report(const marrow::editor::ProjectData& project) {
    std::vector<std::string> rows;
    rows.push_back("notes=" + project.editor_metadata.notes);
    rows.push_back("name=" + project.editor_metadata.name);
    rows.push_back("active_animation=" + project.editor_metadata.active_animation);
    for (const marrow::editor::IkConstraintEdit& edit : project.ik_constraint_edits) {
        std::string row = "ik " + edit.name + " target=" + edit.target_bone_name + " mix=" +
            std::to_string(edit.mix) + " bones=";
        for (const std::string& bone : edit.bone_names) {
            row += bone + ",";
        }
        rows.push_back(row);
    }
    for (const marrow::editor::TransformTimelineEdit& edit : project.transform_timeline_edits) {
        std::string row = "transform " + edit.animation_name + "/" + edit.bone_name +
            " channel=" + std::to_string(static_cast<int>(edit.channel)) + " keys=";
        for (const marrow::editor::TransformKeyframeEdit& key : edit.keyframes) {
            row += std::to_string(key.time) + ":" + std::to_string(key.angle) + ":" +
                std::to_string(key.x) + ":" + std::to_string(key.y) + ",";
        }
        rows.push_back(row);
    }
    for (const marrow::editor::MeshWeightAttachmentEdit& edit :
         project.mesh_weight_attachment_edits) {
        rows.push_back(
            "weights " + edit.skin_name + "/" + edit.slot_name + "/" + edit.attachment_name);
    }
    return rows;
}

/** @brief Everything a rolled-back commit must leave exactly as it found. */
struct PreCommitWitness {
    ByteMap bytes;
    std::uint64_t revision{0};
    std::string active_source;
    std::vector<std::string> provenance;
};

PreCommitWitness capture(const marrow::editor::EditorSession& session) {
    PreCommitWitness witness;
    witness.bytes = bundle_bytes(*session.project());
    witness.revision = session.runtime_revision();
    witness.active_source = active_skeleton_source(session);
    witness.provenance = provenance_rows(*session.project());
    return witness;
}

}  // namespace mar189

/**
 * @brief R3-R8 -- the sweep, provenance, preservation, rollback and refusals.
 *
 * Runs after R0-R2 and shares their attribution order.
 */
bool validate_mar189_commit_rollback(const std::filesystem::path& scratch) {
    using marrow::editor::PsdCommitStep;
    using mar189::ByteMap;
    using mar189::Scenario;

    const std::vector<mar188::SynthLayer> initial_tree = mar188::fixture_tree();
    const std::vector<mar188::SynthLayer> candidate_tree = {
        {{"torso"}, "arm_l", 4, 20, 12, 8, 41U, 51U, 61U},
        {{"torso"}, "body", 16, 12, 20, 24, 71U, 81U, 91U},
    };

    // ---- R3 -- the failpoint sweep, one iteration per kAllCommitSteps entry --
    //
    // A table, not a uniform `!ok`. Fourteen arms expect a rollback and the
    // fifteenth expects a completed commit; a uniform assumption passes fourteen
    // and is wrong about the last one in the direction that destroys a finished
    // reimport.
    {
        struct StepExpectation {
            PsdCommitStep step;
            enum Outcome { RollsBack, SucceedsWithError } outcome;
            /// @brief Adoption legitimately bumps the revision, and so does the
            /// re-adopt a rollback performs. Only the arms before adoption can
            /// assert it unmoved; the arms after it assert the stronger clause --
            /// that the ACTIVE SKELETON SOURCE is byte-identical.
            bool revision_unmoved;
        };
        const std::vector<StepExpectation> expectations = {
            {PsdCommitStep::ValidateRequest, StepExpectation::RollsBack, true},
            {PsdCommitStep::PruneUnpreserved, StepExpectation::RollsBack, true},
            {PsdCommitStep::ValidateStagedBundle, StepExpectation::RollsBack, true},
            {PsdCommitStep::OpenJournal, StepExpectation::RollsBack, true},
            {PsdCommitStep::BackupLayers, StepExpectation::RollsBack, true},
            {PsdCommitStep::BackupTexture, StepExpectation::RollsBack, true},
            {PsdCommitStep::BackupAtlas, StepExpectation::RollsBack, true},
            {PsdCommitStep::BackupSkeleton, StepExpectation::RollsBack, true},
            {PsdCommitStep::PlaceLayers, StepExpectation::RollsBack, true},
            {PsdCommitStep::PlaceTexture, StepExpectation::RollsBack, true},
            {PsdCommitStep::PlaceAtlas, StepExpectation::RollsBack, true},
            {PsdCommitStep::PlaceSkeleton, StepExpectation::RollsBack, true},
            {PsdCommitStep::AdoptRuntimeSources, StepExpectation::RollsBack, false},
            {PsdCommitStep::UpdateProvenance, StepExpectation::RollsBack, false},
            {PsdCommitStep::CleanJournal, StepExpectation::SucceedsWithError, false},
        };
        // A step that exists but has no row is a step the sweep does not cover, and
        // AC3 says "every". The table is checked against the enum by identity, not
        // by size: a size check passes on a table with the right count and the wrong
        // members.
        {
            std::vector<std::string> table;
            for (const StepExpectation& row : expectations) {
                table.emplace_back(marrow::editor::psd_commit_step_name(row.step));
            }
            std::vector<std::string> all;
            for (const PsdCommitStep step : marrow::editor::kAllCommitSteps) {
                all.emplace_back(marrow::editor::psd_commit_step_name(step));
            }
            if (!mar189::expect_steps(table, all, "R3(table)")) {
                return false;
            }
        }

        for (const StepExpectation& row : expectations) {
            const std::string name = marrow::editor::psd_commit_step_name(row.step);
            const std::string label = "R3/" + name;
            Scenario scenario;
            if (!mar189::open_scenario(
                    scratch, "r3_" + name, initial_tree, candidate_tree, &scenario) ||
                !mar189::plan_scenario(&scenario, label.c_str())) {
                return false;
            }
            const mar189::PreCommitWitness before = mar189::capture(scenario.session);
            const std::string staged_skeleton =
                mar188::read_all(scenario.plan.staged_skeleton_path);

            marrow::editor::PsdReimportCommitOptions options;
            options.project_path = scenario.project_path;
            marrow::editor::PsdReimportCommitResult result;
            {
                const mar189::ScopedCommitFailpoint installed(mar189::fail_after(row.step));
                result = marrow::editor::commit_psd_reimport(
                    scenario.session, scenario.plan, options);
            }

            if (result.error.find(name) == std::string::npos) {
                std::cerr << label << ": the error must name the injected step; got '"
                          << result.error << "'.\n";
                return false;
            }
            const std::vector<std::string> residue =
                mar189::journal_residue_scan(*scenario.session.project());
            if (!residue.empty()) {
                std::cerr << label << ": journal residue left in the bundle:\n";
                for (const std::string& path : residue) {
                    std::cerr << "  " << path << '\n';
                }
                return false;
            }

            if (row.outcome == StepExpectation::SucceedsWithError) {
                if (!result.ok) {
                    std::cerr << label
                              << ": a failure after the last step must still report "
                                 "success; the commit reported ok=0 ('"
                              << result.error << "').\n";
                    return false;
                }
                if (result.rolled_back) {
                    std::cerr << label
                              << ": a failure after CleanJournal must not roll a "
                                 "completed reimport back.\n";
                    return false;
                }
                const std::string committed = mar188::read_all(
                    scenario.session.project()->resolved_skeleton_path());
                if (committed != staged_skeleton) {
                    std::cerr << label
                              << ": the committed skeleton must hold the staged bytes; "
                                 "sizes "
                              << committed.size() << " and " << staged_skeleton.size()
                              << ".\n";
                    return false;
                }
                continue;
            }

            if (result.ok) {
                std::cerr << label << ": an injected failure must not report success.\n";
                return false;
            }
            if (!result.rolled_back || !result.rollback_error.empty()) {
                std::cerr << label << ": the commit must roll back cleanly; rolled_back="
                          << result.rolled_back << " rollback_error='"
                          << result.rollback_error << "'.\n";
                return false;
            }
            if (!mar189::expect_bundle_equal(
                    before.bytes, mar189::bundle_bytes(*scenario.session.project()), label)) {
                return false;
            }
            if (mar189::active_skeleton_source(scenario.session) != before.active_source) {
                std::cerr << label
                          << ": the session's active skeleton source must be unchanged.\n";
                return false;
            }
            if (!mar189::expect_rows_equal(
                    mar189::provenance_rows(*scenario.session.project()),
                    before.provenance,
                    label + "(provenance)")) {
                return false;
            }
            if (row.revision_unmoved &&
                scenario.session.runtime_revision() != before.revision) {
                std::cerr << label << ": runtime_revision() must be unmoved; it went from "
                          << before.revision << " to " << scenario.session.runtime_revision()
                          << ".\n";
                return false;
            }
        }
    }

    // ---- R3b -- CleanJournal reports residue rather than failing --------------
    //
    // The sweep cannot produce residue: injecting AFTER `CleanJournal` runs it
    // first, and it succeeds. This makes the removals themselves fail.
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r3b", initial_tree, candidate_tree, &scenario) ||
            !mar189::plan_scenario(&scenario, "R3b")) {
            return false;
        }
        const std::filesystem::path directory = scenario.directory;
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        marrow::editor::PsdReimportCommitResult result;
        {
            // Not a failure injection: the callback returns empty and only makes the
            // project directory unwritable, which is the last thing that happens
            // before `CleanJournal` tries to remove four backups and a manifest.
            const mar189::ScopedCommitFailpoint installed(
                [&directory](PsdCommitStep reached) -> std::string {
                    if (reached == PsdCommitStep::UpdateProvenance) {
                        std::error_code chmod_error;
                        std::filesystem::permissions(
                            directory,
                            std::filesystem::perms::owner_read |
                                std::filesystem::perms::owner_exec,
                            std::filesystem::perm_options::replace,
                            chmod_error);
                    }
                    return {};
                });
            result = marrow::editor::commit_psd_reimport(
                scenario.session, scenario.plan, options);
        }
        std::error_code restore_error;
        std::filesystem::permissions(
            directory,
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::replace,
            restore_error);
        if (!result) {
            std::cerr << "R3b: an unremovable backup must not fail the commit; got '"
                      << result.error << "'.\n";
            return false;
        }
        if (result.rolled_back) {
            std::cerr << "R3b: an unremovable backup must not roll a completed reimport "
                         "back.\n";
            return false;
        }
        if (result.journal_residue.empty()) {
            std::cerr << "R3b: the residue list must name every backup CleanJournal could "
                         "not remove; it is empty.\n";
            return false;
        }
        for (const std::filesystem::path& residue : result.journal_residue) {
            if (!std::filesystem::exists(residue)) {
                std::cerr << "R3b: reported residue '" << residue.generic_string()
                          << "' does not exist.\n";
                return false;
            }
        }
    }

    // ---- R4 -- overlays survive, provenance is rewritten ---------------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(
                scratch, "r4", initial_tree, candidate_tree, &scenario,
                [](marrow::editor::ProjectData* project) {
                    marrow::editor::IkConstraintEdit ik;
                    ik.name = "overlay_ik";
                    ik.bone_names = {"torso"};
                    ik.target_bone_name = "root";
                    ik.mix = 0.5;
                    project->ik_constraint_edits.push_back(std::move(ik));
                    marrow::editor::TransformTimelineEdit transform;
                    transform.animation_name = "idle";
                    transform.bone_name = "torso";
                    transform.channel = marrow::editor::TransformTimelineChannel::Rotate;
                    marrow::editor::TransformKeyframeEdit key;
                    key.time = 0.25;
                    key.angle = 12.0;
                    transform.keyframes.push_back(key);
                    project->transform_timeline_edits.push_back(std::move(transform));
                    project->editor_metadata.notes = "overlay probe";
                }) ||
            !mar189::plan_scenario(&scenario, "R4")) {
            return false;
        }
        const std::vector<std::string> overlays_before = mar189::overlay_report(
            *scenario.session.project());
        std::vector<std::string> expected_provenance;
        expected_provenance.push_back(
            "source=" +
            marrow::editor::project_relative_path(
                scenario.project_path, scenario.candidate_psd)
                .generic_string());
        expected_provenance.push_back(
            "layers=" +
            scenario.session.project()
                ->editor_metadata.import_sources->psd->layers_directory.generic_string());
        for (const marrow::editor::PsdPlannedLayer& layer : scenario.plan.layers) {
            std::string identity;
            for (const std::string& segment : layer.group_path) {
                identity += segment + "|";
            }
            identity += layer.layer_name;
            if (layer.change == marrow::editor::PsdLayerChangeKind::Missing) {
                expected_provenance.push_back(
                    identity + " slot=" + layer.current_slot_name + " attachment=" +
                    layer.current_attachment_name + " bone=" + layer.current_bone_name +
                    " image=" + layer.current_image_file);
                continue;
            }
            expected_provenance.push_back(
                identity + " slot=" + layer.proposed_slot_name + " attachment=" +
                layer.proposed_attachment_name + " bone=" + layer.proposed_bone_name +
                " image=" + layer.proposed_image_file);
        }

        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, scenario.plan, options);
        if (!result) {
            std::cerr << "R4: the commit must succeed; got '" << result.error << "'.\n";
            return false;
        }
        // In memory, never through a save round trip: `Value::Object` is a
        // `std::map` and normalises order, and a 17-digit double does not survive.
        if (!mar189::expect_rows_equal(
                mar189::overlay_report(*scenario.session.project()),
                overlays_before,
                "R4(overlays)")) {
            return false;
        }
        if (!mar189::expect_rows_equal(
                mar189::provenance_rows(*scenario.session.project()),
                expected_provenance,
                "R4(provenance)")) {
            return false;
        }
        // The layer directory lives inside the project folder, so the house rule
        // stores it relative. The candidate PSD deliberately does not, and
        // `project_relative_path` keeps such a reference ABSOLUTE rather than
        // emitting `../` -- so "project-relative" is the RULE having been applied,
        // which the row list above asserts, and not a blanket "no absolute paths".
        const marrow::editor::PsdImportProvenance& stored =
            *scenario.session.project()->editor_metadata.import_sources->psd;
        if (stored.layers_directory != std::filesystem::path("bundle_layers")) {
            std::cerr << "R4: the layers directory must be stored project-relative; got '"
                      << stored.layers_directory.generic_string() << "'.\n";
            return false;
        }
        if (stored.source_path !=
            marrow::editor::project_relative_path(
                scenario.project_path, scenario.candidate_psd)) {
            std::cerr << "R4: the source path must go through project_relative_path; got '"
                      << stored.source_path.generic_string() << "'.\n";
            return false;
        }
    }

    // ---- R5 -- preservation and deletion -------------------------------------
    {
        // Arm 1 -- `preserve == true` keeps the identity's provenance row.
        Scenario preserved;
        if (!mar189::open_scenario(
                scratch, "r5_keep", initial_tree, candidate_tree, &preserved) ||
            !mar189::plan_scenario(&preserved, "R5(keep)")) {
            return false;
        }
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = preserved.project_path;
        const marrow::editor::PsdReimportCommitResult kept =
            marrow::editor::commit_psd_reimport(preserved.session, preserved.plan, options);
        if (!kept) {
            std::cerr << "R5(keep): the commit must succeed; got '" << kept.error << "'.\n";
            return false;
        }
        const std::vector<std::string> kept_rows =
            mar189::provenance_rows(*preserved.session.project());
        if (std::none_of(kept_rows.begin(), kept_rows.end(), [](const std::string& row) {
                return row.rfind("shadow ", 0) == 0;
            })) {
            std::cerr << "R5(keep): a Missing layer with preserve=true must keep its "
                         "provenance row; the committed rows are:\n";
            for (const std::string& row : kept_rows) {
                std::cerr << "  " << row << '\n';
            }
            return false;
        }

        // Arm 2 -- `preserve == false` drops exactly that row and prunes exactly
        // that slot. The staged skeleton is patched to CARRY the slot first,
        // because this importer replaces `slots` wholesale and a dropped layer's
        // slot is already gone -- so without the patch the pruning code is never
        // reached and the case would be green with the prune deleted.
        Scenario dropped;
        if (!mar189::open_scenario(
                scratch, "r5_drop", initial_tree, candidate_tree, &dropped) ||
            !mar189::plan_scenario(&dropped, "R5(drop)")) {
            return false;
        }
        std::string missing_identity;
        for (marrow::editor::PsdPlannedLayer& layer : dropped.plan.layers) {
            if (layer.change == marrow::editor::PsdLayerChangeKind::Missing) {
                layer.preserve = false;
                missing_identity = layer.identity;
            }
        }
        if (missing_identity.empty()) {
            std::cerr << "R5(drop): the plan must carry a Missing layer.\n";
            return false;
        }
        {
            marrow::runtime::json::LoadResult staged =
                marrow::runtime::json::load_document(dropped.plan.staged_skeleton_path);
            if (!staged) {
                std::cerr << "R5(drop): the staged skeleton did not parse.\n";
                return false;
            }
            marrow::runtime::json::Value* slots =
                marrow::runtime::json::find_member(staged.document->root, "slots");
            if (slots == nullptr || !slots->is_array()) {
                std::cerr << "R5(drop): the staged skeleton has no slots array.\n";
                return false;
            }
            // A VALID slot: `attachment` is required, and a planted slot without
            // it makes the un-pruned document fail validation instead -- which
            // reddens this case for the wrong reason and never reaches the slot
            // list that is actually under test. The attachment names a region the
            // candidate atlas really has.
            marrow::runtime::json::Value::Object slot;
            slot.emplace("name", make_string_value("shadow"));
            slot.emplace("bone", make_string_value("root"));
            slot.emplace("attachment", make_string_value("body"));
            slots->as_array().push_back(make_object_value(std::move(slot)));
            if (!write_text_file(
                    dropped.plan.staged_skeleton_path,
                    marrow::runtime::json::serialize_pretty(staged.document->root))) {
                std::cerr << "R5(drop): the patched staged skeleton could not be written.\n";
                return false;
            }
        }
        marrow::editor::PsdReimportCommitOptions drop_options;
        drop_options.project_path = dropped.project_path;
        const marrow::editor::PsdReimportCommitResult drop_result =
            marrow::editor::commit_psd_reimport(dropped.session, dropped.plan, drop_options);
        if (!drop_result) {
            std::cerr << "R5(drop): the commit must succeed; got '" << drop_result.error
                      << "'.\n";
            return false;
        }
        const marrow::runtime::json::LoadResult committed = marrow::runtime::json::load_document(
            dropped.session.project()->resolved_skeleton_path());
        if (!committed) {
            std::cerr << "R5(drop): the committed skeleton did not parse.\n";
            return false;
        }
        const std::vector<std::string> committed_slots =
            mar189::slot_names(*committed.document);
        if (std::find(committed_slots.begin(), committed_slots.end(), "shadow") !=
            committed_slots.end()) {
            std::cerr << "R5(drop): slot 'shadow' must be removed by a preserve=false "
                         "Missing layer; the committed slots are:";
            for (const std::string& slot : committed_slots) {
                std::cerr << ' ' << slot;
            }
            std::cerr << '\n';
            return false;
        }
        const std::vector<std::string> drop_rows =
            mar189::provenance_rows(*dropped.session.project());
        if (std::any_of(drop_rows.begin(), drop_rows.end(), [](const std::string& row) {
                return row.rfind("shadow ", 0) == 0;
            })) {
            std::cerr << "R5(drop): the deleted identity's provenance row must be gone.\n";
            return false;
        }
        std::vector<std::string> kept_without_shadow;
        for (const std::string& row : kept_rows) {
            if (row.rfind("shadow ", 0) != 0) {
                kept_without_shadow.push_back(row);
            }
        }
        std::vector<std::string> drop_without_paths(drop_rows.begin() + 2, drop_rows.end());
        std::vector<std::string> kept_without_paths(
            kept_without_shadow.begin() + 2, kept_without_shadow.end());
        if (!mar189::expect_rows_equal(
                drop_without_paths, kept_without_paths, "R5(drop, other rows)")) {
            return false;
        }
    }

    // ---- R5(c) -- a REQUESTED deletion is not reported as destruction ---------
    //
    // Steps 2 and 3 disagree by construction unless one of them is told about the
    // other. `PruneUnpreserved` erases the skin entry of a `Missing && !preserve`
    // layer; `ValidateStagedBundle` then compares staged skins against current
    // ones and treats a missing identity as destruction. Without the exclusion,
    // step 2's own action trips step 3's refusal and a deletion the user asked for
    // becomes impossible.
    //
    // Inert against today's importer, which erases `skins` outright so there is
    // never anything to prune -- which is exactly why this case plants the
    // entries by hand. An untested branch that is only correct because a
    // neighbouring component is broken is not correct, it is unobserved.
    {
        Scenario scenario;
        if (!mar189::open_scenario(
                scratch, "r5_skins", initial_tree, candidate_tree, &scenario)) {
            return false;
        }
        const std::filesystem::path project_skeleton =
            scenario.directory / (std::string(mar189::kBundleStem) + ".mskl");
        // The project owns a hand-authored skin on the slot that is about to go.
        if (!mar189::plant_skin_attachment(project_skeleton, "shadow", "shadow_mesh",
                                           "shadow", false)) {
            return false;
        }
        if (!mar189::plan_scenario(&scenario, "R5(c)")) {
            return false;
        }
        // The staged bundle carries the same identity. Its region is one the
        // CANDIDATE atlas actually has, because the runtime build still has to
        // succeed -- the point of this case is the structural comparison, not a
        // parse failure.
        if (!mar189::plant_skin_attachment(scenario.plan.staged_skeleton_path, "shadow",
                                           "shadow_mesh", "body", true)) {
            return false;
        }
        for (marrow::editor::PsdPlannedLayer& layer : scenario.plan.layers) {
            if (layer.change == marrow::editor::PsdLayerChangeKind::Missing) {
                layer.preserve = false;
            }
        }
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, scenario.plan, options);
        if (!result) {
            std::cerr << "R5(c): a preserve=false deletion must not be refused as "
                         "destruction by the very step that performed it; got '"
                      << result.error << "'.\n";
            return false;
        }
        const marrow::runtime::json::LoadResult committed =
            marrow::runtime::json::load_document(
                scenario.session.project()->resolved_skeleton_path());
        if (!committed) {
            std::cerr << "R5(c): the committed skeleton did not parse.\n";
            return false;
        }
        const marrow::runtime::json::Value* skins =
            marrow::runtime::json::find_member(committed.document->root, "skins");
        if (skins != nullptr && skins->is_object()) {
            const marrow::runtime::json::Value* skin =
                marrow::runtime::json::find_member(*skins, "default");
            if (skin != nullptr && skin->is_object() &&
                marrow::runtime::json::find_member(*skin, "shadow") != nullptr) {
                std::cerr << "R5(c): the deleted identity's skin entry must be gone "
                             "from the committed skeleton.\n";
                return false;
            }
        }
    }

    // ---- R5(d) -- marking a layer for deletion UNBLOCKS its skin -------------
    //
    // R5(c) needs a planted staged entry to reach the exclusion at all. This is the
    // shape that occurs with the importer as it actually is: the current skeleton
    // has a hand-authored skin, the staged one has none (they are erased
    // wholesale), and the ONLY difference between refusing and committing is
    // whether the user marked that layer for deletion.
    //
    // Both arms, because the pair is the assertion. One arm alone would pass on a
    // commit that ignored `preserve` in either direction.
    for (const bool preserve : {true, false}) {
        const std::string label =
            std::string("R5(d)/") + (preserve ? "preserve" : "delete");
        Scenario scenario;
        if (!mar189::open_scenario(
                scratch, std::string("r5_intent_") + (preserve ? "keep" : "drop"),
                initial_tree, candidate_tree, &scenario)) {
            return false;
        }
        const std::filesystem::path project_skeleton =
            scenario.directory / (std::string(mar189::kBundleStem) + ".mskl");
        if (!mar189::plant_skin_attachment(project_skeleton, "shadow", "shadow_mesh",
                                           "shadow", false)) {
            return false;
        }
        if (!mar189::plan_scenario(&scenario, label.c_str())) {
            return false;
        }
        bool marked = false;
        for (marrow::editor::PsdPlannedLayer& layer : scenario.plan.layers) {
            if (layer.change == marrow::editor::PsdLayerChangeKind::Missing) {
                layer.preserve = preserve;
                marked = true;
            }
        }
        if (!marked) {
            std::cerr << label << ": the plan must carry a Missing layer.\n";
            return false;
        }
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        const marrow::editor::PsdReimportCommitResult result =
            marrow::editor::commit_psd_reimport(scenario.session, scenario.plan, options);

        if (preserve) {
            // Kept: losing the attachment is destruction, and it is refused.
            if (result) {
                std::cerr << label << ": a preserved layer's hand-authored skin must "
                             "not be destroyed; the commit succeeded.\n";
                return false;
            }
            if (result.error.find("default/shadow") == std::string::npos) {
                std::cerr << label << ": the refusal must name the identity; got '"
                          << result.error << "'.\n";
                return false;
            }
            continue;
        }
        // Deleted: losing the attachment is the outcome the user asked for.
        if (!result) {
            std::cerr << label << ": marking the layer for deletion must let the "
                         "reimport proceed -- losing that attachment is the requested "
                         "outcome, not destruction. Got '"
                      << result.error << "'.\n";
            return false;
        }
        const marrow::runtime::json::LoadResult committed =
            marrow::runtime::json::load_document(
                scenario.session.project()->resolved_skeleton_path());
        if (!committed) {
            std::cerr << label << ": the committed skeleton did not parse.\n";
            return false;
        }
        // And the deletion really happened, rather than the check being skipped.
        const marrow::runtime::json::Value* skins =
            marrow::runtime::json::find_member(committed.document->root, "skins");
        if (skins != nullptr && skins->is_object()) {
            const marrow::runtime::json::Value* skin =
                marrow::runtime::json::find_member(*skins, "default");
            if (skin != nullptr && skin->is_object() &&
                marrow::runtime::json::find_member(*skin, "shadow") != nullptr) {
                std::cerr << label << ": the deleted layer's skin entry must be gone.\n";
                return false;
            }
        }
    }

    // ---- R6 -- rollback after a SUCCESSFUL adoption ---------------------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r6", initial_tree, candidate_tree, &scenario) ||
            !mar189::plan_scenario(&scenario, "R6")) {
            return false;
        }
        const mar189::PreCommitWitness before = mar189::capture(scenario.session);
        const std::vector<std::string> slots_before =
            mar189::slot_names(*scenario.session.base_skeleton_document());
        if (std::find(slots_before.begin(), slots_before.end(), "shadow") ==
            slots_before.end()) {
            std::cerr << "R6: the ORIGINAL skeleton must carry the slot this case looks "
                         "for; it does not.\n";
            return false;
        }
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        marrow::editor::PsdReimportCommitResult result;
        {
            const mar189::ScopedCommitFailpoint installed(
                mar189::fail_after(PsdCommitStep::UpdateProvenance));
            result = marrow::editor::commit_psd_reimport(
                scenario.session, scenario.plan, options);
        }
        if (result || !result.rolled_back || !result.rollback_error.empty()) {
            std::cerr << "R6: a failure after adoption must roll back cleanly; ok="
                      << result.ok << " rolled_back=" << result.rolled_back
                      << " rollback_error='" << result.rollback_error << "'.\n";
            return false;
        }
        if (!mar189::expect_bundle_equal(
                before.bytes, mar189::bundle_bytes(*scenario.session.project()), "R6")) {
            return false;
        }
        const std::vector<std::string> slots_after =
            mar189::slot_names(*scenario.session.base_skeleton_document());
        if (slots_after != slots_before) {
            std::cerr << "R6: the session must expose the ORIGINAL skeleton's slots after "
                         "the rollback.\n";
            return false;
        }
        if (std::find(slots_after.begin(), slots_after.end(), "shadow") == slots_after.end()) {
            std::cerr << "R6: 'shadow' exists only in the original skeleton and must be "
                         "back in the session's runtime source.\n";
            return false;
        }
        // The rollback ledger is what says WHICH undo steps ran. The byte map says
        // something is wrong; only this says where.
        if (result.steps_rolled_back.empty()) {
            std::cerr << "R6: the rollback ledger must record the steps it undid.\n";
            return false;
        }
    }

    // ---- R6b -- the ROLLBACK seam, and `rollback_error` reachable -------------
    //
    // Until this case existed the rollback seam was declared and never fired, and
    // `rollback_error` was only ever asserted EMPTY -- so "the rollback reports
    // its own failures" was a claim with no evidence behind it. `advance()` runs
    // only in the commit body and can never fire while the rollback is running,
    // which is exactly why the second, independent seam exists. MAR-190's AC6
    // depends on this being real.
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r6b", initial_tree, candidate_tree, &scenario) ||
            !mar189::plan_scenario(&scenario, "R6b")) {
            return false;
        }
        const mar189::PreCommitWitness before = mar189::capture(scenario.session);
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        marrow::editor::PsdReimportCommitResult result;
        {
            // The commit fails after PlaceSkeleton; the ROLLBACK then fails while
            // undoing PlaceAtlas. Two seams, two different failures, and only the
            // second one is what this case is about.
            const mar189::ScopedCommitFailpoint commit_seam(
                mar189::fail_after(PsdCommitStep::PlaceSkeleton));
            const mar189::ScopedRollbackFailpoint rollback_seam(
                [](PsdCommitStep undone) -> std::string {
                    return undone == PsdCommitStep::PlaceAtlas ? "disk went away" : std::string();
                });
            result = marrow::editor::commit_psd_reimport(
                scenario.session, scenario.plan, options);
        }
        if (result) {
            std::cerr << "R6b: the commit must fail.\n";
            return false;
        }
        const std::string expected =
            "rollback of PlaceAtlas failed: injected failure: disk went away";
        if (result.rollback_error != expected) {
            std::cerr << "R6b: a failing rollback must NAME the step it was undoing. "
                         "Expected '"
                      << expected << "'; got '" << result.rollback_error << "'.\n";
            return false;
        }
        // The ledger is the only thing that says WHICH undo steps ran. Reverse
        // order, starting at the last placement the commit completed, and stopping
        // where the seam fired -- a byte map can say something is wrong and never
        // say where.
        const std::vector<std::string> expected_undo = {"PlaceSkeleton", "PlaceAtlas"};
        if (!mar189::expect_steps(
                mar189::step_names(result.steps_rolled_back), expected_undo, "R6b(ledger)")) {
            return false;
        }
        // And the honest consequence, asserted against the PRE-COMMIT map rather
        // than against a map captured after the fact -- comparing the bundle to
        // itself always passes, which is the same shape as a `cmp` of a file
        // against itself.
        //
        // A rollback that stopped half way did NOT restore the bundle. This is the
        // one place in the suite where a byte map is expected to DIFFER: if it
        // matched, `rollback_error` would be reporting a failure that did not
        // happen, and every other case's "restored byte-for-byte" clause would be
        // meaningless.
        bool restored = true;
        const ByteMap after = mar189::bundle_bytes(*scenario.session.project());
        for (const auto& entry : before.bytes) {
            const auto found = after.find(entry.first);
            if (found == after.end() || found->second != entry.second) {
                restored = false;
                break;
            }
        }
        if (restored) {
            std::cerr << "R6b: the rollback reported '" << result.rollback_error
                      << "' but the bundle came back byte-identical anyway -- then the "
                         "error is describing a failure that did not happen.\n";
            return false;
        }
    }

    // ---- R6c -- the ROLLBACK sweep, one arm per rollback-able step ------------
    //
    // R6b asserts the rollback seam at ONE step. That is a seam that is
    // *injectable* per step and *asserted* at one, and the two are different
    // claims -- MAR-190's AC6 rests on the first, so it needs the evidence base
    // the commit path already has from R3.
    //
    // `rollback_advance` is reached from five sites, covering eleven distinct
    // steps: `UpdateProvenance`, the four `Place*`, the four `Backup*`,
    // `OpenJournal` and `AdoptRuntimeSources`. Every arm fails the COMMIT at
    // `UpdateProvenance` -- the last step before `CleanJournal`, so the rollback
    // walks the whole journal -- and fails the ROLLBACK at its own step.
    {
        const std::vector<PsdCommitStep> rollback_steps = {
            PsdCommitStep::UpdateProvenance,
            PsdCommitStep::PlaceSkeleton,
            PsdCommitStep::PlaceAtlas,
            PsdCommitStep::PlaceTexture,
            PsdCommitStep::PlaceLayers,
            PsdCommitStep::BackupSkeleton,
            PsdCommitStep::BackupAtlas,
            PsdCommitStep::BackupTexture,
            PsdCommitStep::BackupLayers,
            PsdCommitStep::OpenJournal,
            PsdCommitStep::AdoptRuntimeSources,
        };
        // The list above is hand-maintained and, until this block existed, compared
        // to NOTHING -- E17's lesson one level up. R3's table is checked against
        // `kAllCommitSteps` BY IDENTITY precisely because a size check passes on a
        // table with the right count and the wrong members; R6c had neither check,
        // so a new `rollback_advance` call site would have been silently uncovered
        // while the sweep still read complete.
        //
        // There is no product-side list of rollback-able steps to compare against,
        // which is the honest reason it was written this way. But one is DERIVABLE:
        // a commit that fails at the last step before `CleanJournal` rolls the whole
        // journal back, and the ledger it produces IS the set of reachable steps.
        // That turns the sweep's completeness from a reading of five call sites into
        // a measurement.
        {
            Scenario probe;
            if (!mar189::open_scenario(
                    scratch, "r6c_reach", initial_tree, candidate_tree, &probe) ||
                !mar189::plan_scenario(&probe, "R6c(reach)")) {
                return false;
            }
            marrow::editor::PsdReimportCommitOptions probe_options;
            probe_options.project_path = probe.project_path;
            marrow::editor::PsdReimportCommitResult probe_result;
            {
                const mar189::ScopedCommitFailpoint commit_seam(
                    mar189::fail_after(PsdCommitStep::UpdateProvenance));
                probe_result = marrow::editor::commit_psd_reimport(
                    probe.session, probe.plan, probe_options);
            }
            std::vector<std::string> reachable =
                mar189::step_names(probe_result.steps_rolled_back);
            std::sort(reachable.begin(), reachable.end());
            reachable.erase(std::unique(reachable.begin(), reachable.end()), reachable.end());
            std::vector<std::string> swept;
            for (const PsdCommitStep step : rollback_steps) {
                swept.emplace_back(marrow::editor::psd_commit_step_name(step));
            }
            std::sort(swept.begin(), swept.end());
            if (!mar189::expect_steps(swept, reachable, "R6c(coverage)")) {
                std::cerr << "  the sweep's step list must equal the set a full-journal "
                             "rollback actually reaches; a step missing here is a step "
                             "the sweep silently does not cover.\n";
                return false;
            }
        }

        for (const PsdCommitStep step : rollback_steps) {
            const std::string name = marrow::editor::psd_commit_step_name(step);
            const std::string label = "R6c/" + name;
            Scenario scenario;
            if (!mar189::open_scenario(
                    scratch, "r6c_" + name, initial_tree, candidate_tree, &scenario) ||
                !mar189::plan_scenario(&scenario, label.c_str())) {
                return false;
            }
            marrow::editor::PsdReimportCommitOptions options;
            options.project_path = scenario.project_path;
            marrow::editor::PsdReimportCommitResult result;
            {
                const mar189::ScopedCommitFailpoint commit_seam(
                    mar189::fail_after(PsdCommitStep::UpdateProvenance));
                const mar189::ScopedRollbackFailpoint rollback_seam(
                    [step](PsdCommitStep undone) -> std::string {
                        return undone == step ? "injected" : std::string();
                    });
                result = marrow::editor::commit_psd_reimport(
                    scenario.session, scenario.plan, options);
            }
            if (result) {
                std::cerr << label << ": the commit must fail.\n";
                return false;
            }
            const std::string expected =
                "rollback of " + name + " failed: injected failure: injected";
            if (result.rollback_error != expected) {
                std::cerr << label << ": the rollback seam must fire at this step and "
                             "name it. Expected '"
                          << expected << "'; got '" << result.rollback_error << "'.\n";
                return false;
            }
            // The ledger's LAST entry is the step the seam stopped at. Asserting
            // only that the step appears would pass on a rollback that carried on
            // past its own reported failure.
            if (result.steps_rolled_back.empty() ||
                marrow::editor::psd_commit_step_name(result.steps_rolled_back.back()) !=
                    name) {
                std::cerr << label << ": the rollback ledger must END at the injected "
                             "step; it is ";
                for (const std::string& entry :
                     mar189::step_names(result.steps_rolled_back)) {
                    std::cerr << ' ' << entry;
                }
                std::cerr << ".\n";
                return false;
            }
        }
    }

    // ---- R7 -- the journal exists in flight, and is gone after ----------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r7", initial_tree, candidate_tree, &scenario) ||
            !mar189::plan_scenario(&scenario, "R7")) {
            return false;
        }
        const mar189::PreCommitWitness before = mar189::capture(scenario.session);
        std::vector<std::string> in_flight;
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;
        marrow::editor::PsdReimportCommitResult result;
        {
            const std::filesystem::path directory = scenario.directory;
            const mar189::ScopedCommitFailpoint installed(
                [&in_flight, &directory](PsdCommitStep reached) -> std::string {
                    if (reached != PsdCommitStep::BackupSkeleton) {
                        return {};
                    }
                    // The seam can OBSERVE as well as inject, and this is the only
                    // mechanism in the design that can see the journal at all: after
                    // the commit, either outcome has removed it.
                    std::error_code error;
                    for (std::filesystem::recursive_directory_iterator
                             iterator(directory, error),
                         end;
                         iterator != end;
                         iterator.increment(error)) {
                        if (error) {
                            break;
                        }
                        in_flight.push_back(iterator->path().filename().string());
                    }
                    return "observed";
                });
            result = marrow::editor::commit_psd_reimport(
                scenario.session, scenario.plan, options);
        }
        std::vector<std::string> manifests;
        std::vector<std::string> backups;
        for (const std::string& name : in_flight) {
            if (name.rfind(".marrow-psd-journal-", 0) == 0) {
                manifests.push_back(name);
            } else if (name.size() > 4 && name.rfind(".bak") == name.size() - 4) {
                backups.push_back(name);
            }
        }
        if (manifests.size() != 1U) {
            std::cerr << "R7: exactly one journal manifest must exist while the commit is "
                         "in flight; saw "
                      << manifests.size() << ".\n";
            return false;
        }
        // FOUR, not three. The design predicted three on the reading that the
        // injection interrupts `BackupSkeleton`; the seam fires AFTER a step's body
        // succeeds -- which is the whole of what AC3 asks for -- so the skeleton's
        // own backup already exists when the callback runs. Measured, and the
        // prediction corrected rather than the assertion weakened.
        if (backups.size() != 4U) {
            std::cerr << "R7: four backups must exist after BackupSkeleton; saw "
                      << backups.size() << ":";
            for (const std::string& name : backups) {
                std::cerr << ' ' << name;
            }
            std::cerr << '\n';
            return false;
        }
        if (result || !result.rolled_back) {
            std::cerr << "R7: the injected failure must roll back.\n";
            return false;
        }
        if (!mar189::expect_bundle_equal(
                before.bytes, mar189::bundle_bytes(*scenario.session.project()), "R7")) {
            return false;
        }
        const std::vector<std::string> residue =
            mar189::journal_residue_scan(*scenario.session.project());
        if (!residue.empty()) {
            std::cerr << "R7: the journal must be gone after the rollback; found:\n";
            for (const std::string& path : residue) {
                std::cerr << "  " << path << '\n';
            }
            return false;
        }
    }

    // ---- R8 -- refusals without side effects ---------------------------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "r8", initial_tree, candidate_tree, &scenario) ||
            !mar189::plan_scenario(&scenario, "R8")) {
            return false;
        }
        const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
        marrow::editor::PsdReimportCommitOptions options;
        options.project_path = scenario.project_path;

        struct Refusal {
            const char* label;
            marrow::editor::PsdReimportPlan plan;
            std::string expected;
        };
        marrow::editor::PsdReimportPlan errored = scenario.plan;
        errored.error = marrow::editor::PsdReimportPlanError{
            scenario.candidate_psd, "synthetic planning failure"};
        marrow::editor::PsdReimportPlan no_skeleton = scenario.plan;
        no_skeleton.staged_skeleton_path.clear();
        marrow::editor::PsdReimportPlan gone = scenario.plan;
        gone.staged_skeleton_path = scenario.staging_root / "removed.mskl";

        const std::vector<Refusal> refusals = {
            {"errored plan", errored,
             "ValidateRequest: the plan carries an error (synthetic planning failure)"},
            {"empty staged skeleton", no_skeleton,
             "ValidateRequest: the plan's staged_skeleton_path is empty"},
            {"missing staged skeleton", gone,
             "ValidateRequest: the plan's staged_skeleton_path ('" +
                 (scenario.staging_root / "removed.mskl").generic_string() +
                 "') no longer exists"},
        };
        for (const Refusal& refusal : refusals) {
            const marrow::editor::PsdReimportCommitResult result =
                marrow::editor::commit_psd_reimport(scenario.session, refusal.plan, options);
            if (result || result.error != refusal.expected) {
                std::cerr << "R8(" << refusal.label << "): expected '" << refusal.expected
                          << "'; got ok=" << result.ok << " error='" << result.error << "'.\n";
                return false;
            }
            if (!result.steps_executed.empty()) {
                std::cerr << "R8(" << refusal.label
                          << "): a refusal at ValidateRequest must execute no step.\n";
                return false;
            }
            if (!mar189::expect_bundle_equal(
                    before, mar189::bundle_bytes(*scenario.session.project()),
                    std::string("R8(") + refusal.label + ")")) {
                return false;
            }
        }

        marrow::editor::EditorSession empty_session;
        const marrow::editor::PsdReimportCommitResult no_project =
            marrow::editor::commit_psd_reimport(empty_session, scenario.plan, options);
        if (no_project ||
            no_project.error != "ValidateRequest: no editor project is open") {
            std::cerr << "R8(no project): expected 'ValidateRequest: no editor project is "
                         "open'; got ok="
                      << no_project.ok << " error='" << no_project.error << "'.\n";
            return false;
        }

        // The state of the ONLY project fixture in this tree, and inside AC6's
        // "missing inputs": a project that never came from a PSD has no layer
        // directory to replace and no stored identity to preserve against.
        Scenario bare;
        if (!mar189::open_scenario(scratch, "r8_bare", initial_tree, candidate_tree, &bare)) {
            return false;
        }
        {
            const marrow::editor::ProjectLoadResult loaded =
                marrow::editor::load_project(bare.project_path);
            if (!loaded) {
                std::cerr << "R8(no provenance): the project did not load.\n";
                return false;
            }
            marrow::editor::ProjectData stripped = *loaded.project;
            stripped.editor_metadata.import_sources.reset();
            if (!marrow::editor::save_project(stripped, bare.project_path)) {
                std::cerr << "R8(no provenance): the stripped project could not be saved.\n";
                return false;
            }
        }
        if (!bare.session.open(bare.project_path)) {
            std::cerr << "R8(no provenance): the stripped project did not open.\n";
            return false;
        }
        marrow::editor::PsdReimportCommitOptions bare_options;
        bare_options.project_path = bare.project_path;
        const marrow::editor::PsdReimportCommitResult bare_result =
            marrow::editor::commit_psd_reimport(bare.session, scenario.plan, bare_options);
        const std::string expected_bare =
            "ValidateRequest: the project carries no PSD provenance, so there is no layer "
            "directory to replace and no stored identity to preserve against";
        if (bare_result || bare_result.error != expected_bare) {
            std::cerr << "R8(no provenance): expected '" << expected_bare << "'; got ok="
                      << bare_result.ok << " error='" << bare_result.error << "'.\n";
            return false;
        }
    }

    std::cout << "MAR-189 R3-R8: an injected failure after each of the fifteen steps rolls "
                 "the bundle back byte-for-byte and leaves the session's runtime source and "
                 "provenance untouched, except after CleanJournal where the completed "
                 "reimport stands; an unremovable backup is reported as residue rather than "
                 "rolled back; overlays survive a commit element-wise in memory while "
                 "provenance is rewritten in the plan's order with project-relative paths; "
                 "preserve=false drops exactly one identity and one slot; a rollback after a "
                 "successful adoption restores the original skeleton to the session; the "
                 "journal exists in flight and is gone after; and five refusals each name "
                 "their cause and touch nothing.\n";
    return true;
}

namespace mar189 {

/** @brief Dispatches one JSON command against a session, UI-free. */
marrow::editor::AgentDispatchResult dispatch(
    marrow::editor::EditorSession& session,
    marrow::editor::AgentControlState& control,
    const std::string& command) {
    const marrow::runtime::json::LoadResult parsed =
        marrow::runtime::json::parse_document(command);
    if (!parsed) {
        marrow::editor::AgentDispatchResult failed;
        failed.message = "the command did not parse: " + parsed.error->message;
        return failed;
    }
    marrow::editor::AgentCommandContext context{session, control};
    marrow::editor::AgentCommandDispatcher dispatcher;
    return dispatcher.dispatch(context, parsed.document->root);
}

const marrow::runtime::json::Value* member(
    const marrow::runtime::json::Value& object,
    std::string_view name) {
    return object.is_object() ? marrow::runtime::json::find_member(object, name) : nullptr;
}

}  // namespace mar189

/**
 * @brief A1-A6 -- the agent operation and its approval, on disposable bundles.
 *
 * These drive `AgentCommandDispatcher` and `apply_agent_review` DIRECTLY rather
 * than through the C ABI, because `MarrowProject` is opaque outside `marrow_c.cpp`
 * and approval needs the session and the review queue. `agent_dispatch_smoke`
 * keeps the ABI-level dry-run and review invocations and owns A7.
 */
bool validate_mar189_agent_operation(const std::filesystem::path& scratch) {
    using mar189::ByteMap;
    using mar189::Scenario;

    const std::vector<mar188::SynthLayer> initial_tree = mar188::fixture_tree();
    const std::vector<mar188::SynthLayer> candidate_tree = {
        {{"torso"}, "arm_l", 4, 20, 12, 8, 41U, 51U, 61U},
        {{"torso"}, "body", 16, 12, 20, 24, 71U, 81U, 91U},
    };

    // ---- A1 -- a dry run returns a plan and writes nothing -------------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "a1", initial_tree, candidate_tree, &scenario)) {
            return false;
        }
        if (!scenario.session.open(scenario.project_path)) {
            std::cerr << "A1: the project did not open.\n";
            return false;
        }
        const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
        marrow::editor::AgentControlState control;
        const std::filesystem::path staging = scratch / "a1_staging";
        const marrow::editor::AgentDispatchResult result = mar189::dispatch(
            scenario.session,
            control,
            "{\"op\":\"import.psd_layers\",\"args\":{\"input\":\"" +
                scenario.candidate_psd.generic_string() + "\",\"staging_root\":\"" +
                staging.generic_string() + "\",\"dry_run\":true}}");
        if (!result.ok) {
            std::cerr << "A1: the dry run must succeed; got '" << result.message << "'.\n";
            return false;
        }
        const marrow::runtime::json::Value* plan =
            mar189::member(result.scene_delta, "plan");
        if (plan == nullptr) {
            std::cerr << "A1: the dry run must return a 'plan' object.\n";
            return false;
        }
        const marrow::runtime::json::Value* layers = mar189::member(*plan, "layers");
        if (layers == nullptr || !layers->is_array() || layers->as_array().empty()) {
            std::cerr << "A1: the plan must carry layer rows.\n";
            return false;
        }
        for (const char* count : {"added", "updated", "missing"}) {
            const marrow::runtime::json::Value* value = mar189::member(*plan, count);
            if (value == nullptr || !value->is_number()) {
                std::cerr << "A1: the plan must carry the '" << count << "' count.\n";
                return false;
            }
        }
        if (!mar189::expect_bundle_equal(
                before, mar189::bundle_bytes(*scenario.session.project()), "A1")) {
            return false;
        }
        // The staging root is removed in both directions -- a dry run leaves
        // nothing behind, and a queued review does not own a filesystem lifetime
        // across an unbounded human wait.
        std::error_code error;
        if (std::filesystem::exists(staging, error)) {
            for (std::filesystem::directory_iterator iterator(staging, error), end;
                 iterator != end;
                 iterator.increment(error)) {
                std::cerr << "A1: the dry run left '"
                          << iterator->path().generic_string() << "' behind.\n";
                return false;
            }
        }
    }

    // ---- A2 -- an output naming a non-project path is refused ---------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "a2", initial_tree, candidate_tree, &scenario)) {
            return false;
        }
        if (!scenario.session.open(scenario.project_path)) {
            std::cerr << "A2: the project did not open.\n";
            return false;
        }
        marrow::editor::AgentControlState control;
        const marrow::editor::AgentDispatchResult result = mar189::dispatch(
            scenario.session,
            control,
            "{\"op\":\"import.psd_layers\",\"args\":{\"input\":\"" +
                scenario.candidate_psd.generic_string() +
                "\",\"output\":\"/tmp/mar189_not_the_project.mskl\",\"dry_run\":true}}");
        if (result.ok || result.error_code != "not_project_bundle") {
            std::cerr << "A2: an output outside the project bundle must be refused with "
                         "not_project_bundle; got ok="
                      << result.ok << " code='" << result.error_code << "' message='"
                      << result.message << "'.\n";
            return false;
        }
        if (result.message.find("replaces the project's own bundle") == std::string::npos) {
            std::cerr << "A2: the refusal must say why; got '" << result.message << "'.\n";
            return false;
        }
    }

    // ---- A3 -- a staging root outside the whitelist is refused ---------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "a3", initial_tree, candidate_tree, &scenario)) {
            return false;
        }
        if (!scenario.session.open(scenario.project_path)) {
            std::cerr << "A3: the project did not open.\n";
            return false;
        }
        marrow::editor::AgentControlState control;
        // `agent_path_allowed` whitelists the project directory, the export
        // directory, `/tmp` and `/private/tmp`. A user's home is none of them.
        const marrow::editor::AgentDispatchResult result = mar189::dispatch(
            scenario.session,
            control,
            "{\"op\":\"import.psd_layers\",\"args\":{\"input\":\"" +
                scenario.candidate_psd.generic_string() +
                "\",\"staging_root\":\"/usr/local/mar189_forbidden\",\"dry_run\":true}}");
        if (result.ok || result.error_code != "forbidden_path" ||
            result.message != "Staging root is outside the agent whitelist.") {
            std::cerr << "A3: expected forbidden_path for a staging_root outside the "
                         "whitelist; got ok="
                      << result.ok << " code='" << result.error_code << "' message='"
                      << result.message << "'.\n";
            return false;
        }
    }

    // ---- A4/A5 -- a review is queued with a digest, and approval commits -----
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "a4", initial_tree, candidate_tree, &scenario)) {
            return false;
        }
        if (!scenario.session.open(scenario.project_path)) {
            std::cerr << "A4: the project did not open.\n";
            return false;
        }
        const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
        marrow::editor::AgentControlState control;
        const std::string command =
            "{\"op\":\"import.psd_layers\",\"args\":{\"input\":\"" +
            scenario.candidate_psd.generic_string() + "\",\"staging_root\":\"" +
            (scratch / "a4_staging").generic_string() + "\",\"dry_run\":false}}";
        const marrow::editor::AgentDispatchResult queued =
            mar189::dispatch(scenario.session, control, command);
        if (!queued.ok || !queued.requires_review) {
            std::cerr << "A4: a non-dry run must queue a review; got ok=" << queued.ok
                      << " message='" << queued.message << "'.\n";
            return false;
        }
        if (control.review_queue.size() != 1U) {
            std::cerr << "A4: exactly one review must be queued; got "
                      << control.review_queue.size() << ".\n";
            return false;
        }
        const marrow::editor::AgentReviewRequest& request = control.review_queue.front();
        if (request.kind != marrow::editor::AgentReviewKind::ImportOrPack ||
            request.plan_digest.empty() || request.input_path.empty()) {
            std::cerr << "A4: the queued request must carry the input path and a "
                         "non-empty plan digest; digest='"
                      << request.plan_digest << "' input='"
                      << request.input_path.generic_string() << "'.\n";
            return false;
        }
        if (!request.allowed) {
            std::cerr << "A4: the project's own bundle must pass the whitelist; the "
                         "request was rejected with '"
                      << request.message << "'.\n";
            return false;
        }
        if (!mar189::expect_bundle_equal(
                before, mar189::bundle_bytes(*scenario.session.project()), "A4")) {
            return false;
        }

        // A5 -- approval commits.
        const std::uint64_t review_id = request.id;
        const marrow::editor::AgentDispatchResult applied = marrow::editor::apply_agent_review(
            scenario.session, control, review_id);
        if (!applied.ok) {
            std::cerr << "A5: approving an allowed request must commit; got '"
                      << applied.message << "' (" << applied.error_code << ").\n";
            return false;
        }
        if (!control.review_queue.empty()) {
            std::cerr << "A5: the request must be gone from the queue after a commit.\n";
            return false;
        }
        const std::vector<std::string> provenance =
            mar189::provenance_rows(*scenario.session.project());
        if (provenance.empty() ||
            provenance.front() !=
                "source=" +
                    marrow::editor::project_relative_path(
                        scenario.project_path, scenario.candidate_psd)
                        .generic_string()) {
            std::cerr << "A5: the committed provenance must name the approved PSD; got '"
                      << (provenance.empty() ? std::string("<none>") : provenance.front())
                      << "'.\n";
            return false;
        }
        const std::string committed = mar188::read_all(
            scenario.session.project()->resolved_skeleton_path());
        const std::string original = before.at(
            scenario.session.project()
                ->resolved_skeleton_path()
                .lexically_normal()
                .generic_string());
        if (committed == original) {
            std::cerr << "A5: the skeleton must have been replaced.\n";
            return false;
        }
    }

    // ---- A6 -- approval boundaries ------------------------------------------
    {
        Scenario scenario;
        if (!mar189::open_scenario(scratch, "a6", initial_tree, candidate_tree, &scenario)) {
            return false;
        }
        if (!scenario.session.open(scenario.project_path)) {
            std::cerr << "A6: the project did not open.\n";
            return false;
        }
        marrow::editor::AgentControlState control;

        // (i) an unknown id.
        const marrow::editor::AgentDispatchResult unknown =
            marrow::editor::apply_agent_review(scenario.session, control, 4242U);
        if (unknown.ok || unknown.error_code != "unknown_review") {
            std::cerr << "A6(unknown): expected unknown_review; got ok=" << unknown.ok
                      << " code='" << unknown.error_code << "'.\n";
            return false;
        }

        // (ii) a request the whitelist rejected. `allowed` is the verdict recorded
        // at enqueue time, so the check cannot be satisfied by re-deriving it.
        {
            marrow::editor::AgentReviewRequest rejected;
            rejected.id = 7U;
            rejected.kind = marrow::editor::AgentReviewKind::ImportOrPack;
            rejected.op = "import.psd_layers";
            rejected.input_path = scenario.candidate_psd;
            rejected.plan_digest = "whatever";
            rejected.allowed = false;
            control.review_queue.push_back(rejected);
            const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
            const marrow::editor::AgentDispatchResult refused =
                marrow::editor::apply_agent_review(scenario.session, control, 7U);
            if (refused.ok || refused.error_code != "forbidden_path") {
                std::cerr << "A6(rejected): approving a whitelist-rejected request must "
                             "refuse; got ok="
                          << refused.ok << " code='" << refused.error_code << "'.\n";
                return false;
            }
            if (!mar189::expect_bundle_equal(
                    before, mar189::bundle_bytes(*scenario.session.project()),
                    "A6(rejected)")) {
                return false;
            }
            control.review_queue.clear();
        }

        // (iii) a PSD mutated between review and approval.
        {
            const std::string command =
                "{\"op\":\"import.psd_layers\",\"args\":{\"input\":\"" +
                scenario.candidate_psd.generic_string() + "\",\"staging_root\":\"" +
                (scratch / "a6_staging").generic_string() + "\",\"dry_run\":false}}";
            const marrow::editor::AgentDispatchResult queued =
                mar189::dispatch(scenario.session, control, command);
            if (!queued.ok || control.review_queue.size() != 1U) {
                std::cerr << "A6(changed): the review did not queue.\n";
                return false;
            }
            const std::uint64_t review_id = control.review_queue.front().id;
            // A layer added and a layer renamed: the ordered row list changes and
            // the digest with it. A count would not move.
            const std::vector<mar188::SynthLayer> mutated = {
                {{"torso"}, "arm_r", 4, 20, 12, 8, 41U, 51U, 61U},
                {{"torso"}, "body", 16, 12, 20, 24, 71U, 81U, 91U},
                {{}, "halo", 2, 2, 6, 6, 9U, 9U, 9U},
            };
            if (!mar188::write_synthetic_psd(scenario.candidate_psd, 64, 64, mutated)) {
                std::cerr << "A6(changed): the mutated PSD could not be written.\n";
                return false;
            }
            const ByteMap before = mar189::bundle_bytes(*scenario.session.project());
            const marrow::editor::AgentDispatchResult refused =
                marrow::editor::apply_agent_review(scenario.session, control, review_id);
            if (refused.ok || refused.error_code != "psd_changed_since_review") {
                std::cerr << "A6(changed): a PSD changed since review must be refused with "
                             "psd_changed_since_review; got ok="
                          << refused.ok << " code='" << refused.error_code << "' message='"
                          << refused.message << "'.\n";
                return false;
            }
            if (!mar189::expect_bundle_equal(
                    before, mar189::bundle_bytes(*scenario.session.project()),
                    "A6(changed)")) {
                return false;
            }
            if (control.review_queue.size() != 1U) {
                std::cerr << "A6(changed): a refused approval must leave the request "
                             "queued; the queue holds "
                          << control.review_queue.size() << ".\n";
                return false;
            }
        }
    }

    std::cout << "MAR-189 A1-A6: a dry run returns the ordered plan with its three counts "
                 "and leaves both the bundle and the staging root empty; an output outside "
                 "the project bundle and a staging root outside the whitelist are each "
                 "refused by code and message; a non-dry run queues one review carrying the "
                 "input path and a plan digest; approving it commits and empties the queue; "
                 "and an unknown id, a whitelist-rejected request and a PSD changed since "
                 "review are each refused without touching a byte.\n";
    return true;
}

// ===================== MAR-190 -- the reimport review model =====================

namespace mar190 {

/**
 * @brief The stored side of MAR-190's fixture: five layers, three groups deep.
 *
 * Deliberately larger than `mar188::fixture_tree()`, which has three layers and
 * therefore cannot produce 3 `Updated` and 2 `Missing` at once. V1 needs the
 * three sections to INTERLEAVE in identity order, so that a grouping bug that
 * preserves membership but loses order is visible.
 */
std::vector<mar188::SynthLayer> base_tree() {
    return {
        {{"fx"}, "halo", 2, 2, 6, 6, 1U, 2U, 3U},
        {{}, "shadow", 18, 40, 14, 8, 10U, 20U, 30U},
        {{"torso"}, "arm_l", 4, 20, 12, 8, 40U, 50U, 60U},
        {{"torso"}, "arm_r", 30, 20, 12, 8, 41U, 51U, 61U},
        {{"torso"}, "body", 16, 12, 20, 24, 70U, 80U, 90U},
    };
}

/**
 * @brief The candidate: keeps three, drops two, adds two.
 *
 * Against `base_tree()` this yields the union below, in the plan's own ascending
 * identity order, with every section interleaved:
 *
 *   0 `fx|glow`      Added      4 `torso|arm_r`  Missing
 *   1 `fx|halo`      Updated    5 `torso|body`   Updated
 *   2 `shadow`       Updated    6 `torso|hand_r` Added
 *   3 `torso|arm_l`  Missing
 */
std::vector<mar188::SynthLayer> candidate_tree() {
    return {
        {{"fx"}, "glow", 8, 2, 6, 6, 4U, 5U, 6U},
        {{"fx"}, "halo", 2, 2, 6, 6, 1U, 2U, 3U},
        {{}, "shadow", 18, 40, 14, 8, 10U, 20U, 30U},
        {{"torso"}, "body", 16, 12, 20, 24, 70U, 80U, 90U},
        {{"torso"}, "hand_r", 30, 30, 6, 6, 11U, 12U, 13U},
    };
}

/**
 * @brief AC3's WHOLE invariant, in one helper, called by every no-op case.
 *
 * Five distinct paths reach "nothing changed" -- cancel, modal close, stale,
 * planning failure and commit failure. Five partial assertions would let each
 * path prove a different subset and none prove the invariant, so every caller
 * runs every clause. Deliberately NOT asserted: `status_message`. A failed
 * reimport should say so, and AC3 does not list it; its absence here is a
 * decision rather than an oversight.
 */
struct NoOpWitness {
    std::string serialized;
    std::uint64_t project_revision{0};
    std::uint64_t runtime_revision{0};
    std::uint64_t preview_revision{0};
    std::size_t undo_count{0};
    std::size_t redo_count{0};
    mar189::ByteMap bundle;
    std::filesystem::path skeleton_path;
    std::vector<std::filesystem::path> atlas_paths;
};

NoOpWitness capture_no_op(const marrow::editor::EditorSession& session) {
    NoOpWitness witness;
    const marrow::editor::ProjectData& project = *session.project();
    // In memory, never round-tripped through a file: `serialize_project` is not
    // bit-exact for doubles needing 17 significant digits, so a file round trip
    // would drift under the comparison.
    witness.serialized = marrow::editor::serialize_project(project);
    witness.project_revision = session.project_revision();
    witness.runtime_revision = session.runtime_revision();
    witness.preview_revision = session.preview_revision();
    witness.undo_count = session.undo_count();
    witness.redo_count = session.redo_count();
    witness.bundle = mar189::bundle_bytes(project);
    witness.skeleton_path = project.resolved_skeleton_path();
    witness.atlas_paths = project.resolved_atlas_paths();
    return witness;
}

/**
 * @brief One `"<identity> -> slot,attachment,bone,image"` row per stored layer.
 *
 * The FULL tuple, in the stored order. AC2's observable is this list and nothing
 * else: `preserve` decides whether a `Missing` layer keeps its row here, and a
 * row's presence is the only thing the choice changes. It does NOT keep the
 * layer's slot in the committed skeleton -- the importer replaces `slots`
 * wholesale -- so no case may assert that.
 */
std::vector<std::string> provenance_rows(const marrow::editor::ProjectData& project) {
    std::vector<std::string> rows;
    if (!project.editor_metadata.import_sources.has_value() ||
        !project.editor_metadata.import_sources->psd.has_value()) {
        return rows;
    }
    for (const marrow::editor::PsdLayerProvenance& layer :
         project.editor_metadata.import_sources->psd->layers) {
        std::string identity;
        for (const std::string& segment : layer.group_path) {
            identity += segment;
            identity += '|';
        }
        identity += layer.layer_name;
        rows.push_back(
            identity + " -> " + layer.slot_name + "," + layer.attachment_name + "," +
            layer.bone_name + "," + layer.image_file);
    }
    return rows;
}

/** @brief Slot names the committed skeleton document defines, sorted. */
std::vector<std::string> committed_slot_names(const std::filesystem::path& skeleton_path) {
    std::vector<std::string> names;
    const marrow::runtime::json::LoadResult loaded =
        marrow::runtime::json::load_document(skeleton_path);
    if (!loaded) {
        return names;
    }
    const marrow::runtime::json::Value* slots =
        marrow::runtime::json::find_member(loaded.document->root, "slots");
    if (slots == nullptr || !slots->is_array()) {
        return names;
    }
    for (const marrow::runtime::json::Value& slot : slots->as_array()) {
        const marrow::runtime::json::Value* name =
            marrow::runtime::json::find_member(slot, "name");
        if (name != nullptr && name->is_string()) {
            names.push_back(name->as_string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

/**
 * @brief Runs every clause of AC3's invariant.
 *
 * @param provenance_reverted Set ONLY for the arm that reverts a completed
 *        `UpdateProvenance`, where the authored revision advances by design (see
 *        below). It does NOT relax any history-stack clause.
 * @param revisions_may_move Set ONLY for a failure injected after
 *        `AdoptRuntimeSources`. Adoption bumps the runtime and preview revisions
 *        legitimately, and the rollback's re-adopt bumps them again, so a
 *        revisions-unmoved clause on those arms fails on CORRECT code. Every
 *        other clause -- serialization, both history depths, the whole byte map,
 *        and the runtime source PATHS -- still runs, so the arm is not weakened
 *        anywhere it can be strong. MAR-189's R3 records the same asymmetry.
 */
bool expect_reimport_no_op(
    const marrow::editor::EditorSession& session,
    const NoOpWitness& before,
    std::string_view label,
    bool revisions_may_move = false,
    bool provenance_reverted = false) {
    const marrow::editor::ProjectData& project = *session.project();
    bool ok = true;

    // (a) The authored project, by full byte identity. A dirty flag, a revision
    //     number or "it still loads" are all compatible with an authored edit.
    const std::string after = marrow::editor::serialize_project(project);
    if (after != before.serialized) {
        std::size_t offset = 0;
        const std::size_t shared = std::min(after.size(), before.serialized.size());
        while (offset < shared && after[offset] == before.serialized[offset]) {
            ++offset;
        }
        std::cerr << label << ": the project's serialization changed at offset " << offset
                  << " (before " << before.serialized.size() << " bytes, after "
                  << after.size() << ").\n";
        ok = false;
    }

    // (b) Revisions. (a) cannot see a runtime swap rolled back into identical
    //     FILES that nonetheless left the session rebuilt.
    // The AUTHORED revision must never move on a no-op path, adoption or not: a
    // rolled-back commit that left an authored edit behind is exactly what this
    // catches, and adoption does not touch it.
    // INTENDED, not excused, and the distinction matters because the two read
    // identically here. `apply_history` bumps `project_revision` whenever the
    // project changed (`session.cpp:1672`), in EITHER direction -- so a revert
    // advances it exactly as the edit did. The counter counts CHANGES and is a
    // change detector, not a state identifier; a monotonic counter is what makes
    // it usable as one. AC3's "history unchanged" is about the undo/redo stacks,
    // which the clause below asserts in full.
    if (!provenance_reverted && session.project_revision() != before.project_revision) {
        std::cerr << label << ": the authored project revision moved ("
                  << before.project_revision << " -> " << session.project_revision()
                  << ").\n";
        ok = false;
    }
    if (!revisions_may_move &&
        (session.runtime_revision() != before.runtime_revision ||
         session.preview_revision() != before.preview_revision)) {
        std::cerr << label << ": a runtime revision moved (runtime "
                  << before.runtime_revision << "->" << session.runtime_revision()
                  << ", preview " << before.preview_revision << "->"
                  << session.preview_revision() << ").\n";
        ok = false;
    }

    // (c) BOTH depths. A failed path that opened and rolled back a transaction
    //     can leave undo right and redo wrong.
    // BOTH depths, on EVERY arm including the one that reverts a completed
    // `UpdateProvenance`. This clause used to expect `redo + 1` there, which
    // encoded a defect rather than a requirement: the rollback called
    // `session.undo()`, whose contract is to make the entry REDOABLE, so a failed
    // reimport left Redo armed to re-apply the provenance edit over rolled-back
    // files. AC3 says a commit failure leaves history unchanged, so that was
    // MAR-190's own criterion failing. `commit_psd_reimport` now calls
    // `revert_last_edit()` and the expectation is simply "unchanged".
    if (session.undo_count() != before.undo_count ||
        session.redo_count() != before.redo_count) {
        std::cerr << label << ": history depth moved (undo " << before.undo_count << "->"
                  << session.undo_count() << ", redo " << before.redo_count << "->"
                  << session.redo_count() << ").\n";
        ok = false;
    }

    // (d) Every byte of the bundle and the layer directory, both directions.
    //     `rolled_back == true` is compatible with every byte being wrong.
    const mar189::ByteMap after_bundle = mar189::bundle_bytes(project);
    if (!mar189::expect_bundle_equal(before.bundle, after_bundle, label)) {
        ok = false;
    }
    for (const auto& entry : after_bundle) {
        if (before.bundle.find(entry.first) == before.bundle.end()) {
            std::cerr << label << ": only in after: " << entry.first << '\n';
            ok = false;
        }
    }

    // (e) The runtime SOURCE is a path, not only bytes. AC3 names it.
    if (project.resolved_skeleton_path() != before.skeleton_path) {
        std::cerr << label << ": the active skeleton path changed ("
                  << before.skeleton_path.generic_string() << " -> "
                  << project.resolved_skeleton_path().generic_string() << ").\n";
        ok = false;
    }
    if (project.resolved_atlas_paths() != before.atlas_paths) {
        std::cerr << label << ": the resolved atlas path list changed.\n";
        ok = false;
    }
    return ok;
}

/** @brief One `"<identity> -> <kind>"` row per index, in the section's own order. */
std::vector<std::string> section_rows(
    const marrow::editor::PsdReimportPlan& plan,
    const std::vector<std::size_t>& indices) {
    std::vector<std::string> rows;
    rows.reserve(indices.size());
    for (const std::size_t index : indices) {
        if (index >= plan.layers.size()) {
            rows.push_back("<out of range: " + std::to_string(index) + ">");
            continue;
        }
        rows.push_back(plan.layers[index].identity);
    }
    return rows;
}

/**
 * @brief Asserts the three sections partition `plan.layers` exactly.
 *
 * Union equals every index and the three are pairwise disjoint. A count clause
 * cannot see I1 (a `Missing` index appended to `updated`), because every count
 * still sums to `layers.size()`.
 */
bool expect_partition(
    std::string_view label,
    const marrow::editor::PsdReviewSections& sections,
    std::size_t layer_count) {
    std::vector<std::size_t> seen;
    for (const std::vector<std::size_t>* list :
         {&sections.added, &sections.updated, &sections.missing}) {
        seen.insert(seen.end(), list->begin(), list->end());
    }
    std::sort(seen.begin(), seen.end());
    const auto duplicate = std::adjacent_find(seen.begin(), seen.end());
    if (duplicate != seen.end()) {
        std::cerr << label << ": the three sections do not partition plan.layers -- index "
                  << *duplicate << " appears in more than one section.\n";
        return false;
    }
    std::vector<std::size_t> expected(layer_count);
    for (std::size_t index = 0; index < layer_count; ++index) {
        expected[index] = index;
    }
    if (seen != expected) {
        std::cerr << label << ": the three sections do not cover plan.layers exactly (union "
                     "has "
                  << seen.size() << " indices, expected " << layer_count << ").\n";
        for (const std::size_t index : expected) {
            if (std::find(seen.begin(), seen.end(), index) == seen.end()) {
                std::cerr << "  uncovered index: " << index << '\n';
            }
        }
        return false;
    }
    return true;
}

} // namespace mar190

/**
 * @brief MAR-190 V1-V2. The review model's grouping and its confirmation gate.
 *
 * Runs after every MAR-188 and MAR-189 case, so a synthesiser regression is
 * still attributed to Q0 and a commit regression to MAR-189's own cases rather
 * than to this story.
 */
bool validate_mar190_reimport_review(const std::filesystem::path& scratch) {
    std::error_code directory_error;
    std::filesystem::remove_all(scratch, directory_error);
    std::filesystem::create_directories(scratch, directory_error);

    const std::filesystem::path project_directory = scratch / "project";
    std::filesystem::create_directories(project_directory, directory_error);
    const std::filesystem::path project_path = project_directory / "review.marrow";

    // The stored side, built by the REAL importer over a synthesised PSD, so the
    // provenance is shaped exactly as an import would have written it.
    const std::filesystem::path base_psd = scratch / "base.psd";
    if (!mar188::write_synthetic_psd(base_psd, 64, 64, mar190::base_tree())) {
        std::cerr << "MAR-190 V1: the synthesiser could not write the base PSD.\n";
        return false;
    }
    marrow::editor::PsdImportResult base_import;
    if (!mar188::import_synthetic(base_psd, scratch / "base_out", "base", &base_import)) {
        std::cerr << "MAR-190 V1: the base PSD did not import.\n";
        return false;
    }
    const marrow::editor::ProjectData project = mar188::project_with_provenance(
        project_path,
        marrow::editor::make_psd_provenance(base_import, project_path, base_psd));

    std::size_t staging_index = 0;
    const auto plan_for = [&](const std::vector<mar188::SynthLayer>& layers,
                              const std::string& name,
                              marrow::editor::PsdReimportPlan* plan_out) {
        const std::filesystem::path psd = scratch / (name + ".psd");
        if (!mar188::write_synthetic_psd(psd, 64, 64, layers)) {
            std::cerr << "MAR-190 " << name << ": the synthesiser could not write it.\n";
            return false;
        }
        marrow::editor::PsdReimportPlanOptions options;
        options.psd_path = psd;
        options.staging_root = scratch / ("staging_" + std::to_string(staging_index++));
        *plan_out = marrow::editor::plan_psd_reimport(project, options);
        return true;
    };

    marrow::editor::PsdReimportPlan plan;
    if (!plan_for(mar190::candidate_tree(), "V1", &plan)) {
        return false;
    }
    if (!plan) {
        std::cerr << "MAR-190 V1: planning failed: " << plan.error->format() << '\n';
        return false;
    }

    // ---- V1 -- grouping is the model's, not the view's. --------------------
    {
        // The whole plan first, so a fixture drift is reported as a fixture
        // problem rather than as a grouping problem.
        const std::vector<std::string> expected_plan = {
            "fx|glow -> Added",     "fx|halo -> Updated",  "shadow -> Updated",
            "torso|arm_l -> Missing", "torso|arm_r -> Missing", "torso|body -> Updated",
            "torso|hand_r -> Added",
        };
        if (!mar188::expect_rows("MAR-190 V1 (the fixture's own plan)",
                                 mar188::plan_rows(plan), expected_plan)) {
            return false;
        }

        const marrow::editor::PsdReviewSections sections =
            marrow::editor::group_psd_review(plan);

        // Element-wise ORDERED comparison, per section. Set membership passes
        // under a reversed iteration; only this clause sees it.
        if (!mar188::expect_rows("MAR-190 V1 section 'added' (ordered)",
                                 mar190::section_rows(plan, sections.added),
                                 {"fx|glow", "torso|hand_r"})) {
            return false;
        }
        if (!mar188::expect_rows("MAR-190 V1 section 'updated' (ordered)",
                                 mar190::section_rows(plan, sections.updated),
                                 {"fx|halo", "shadow", "torso|body"})) {
            return false;
        }
        if (!mar188::expect_rows("MAR-190 V1 section 'missing' (ordered)",
                                 mar190::section_rows(plan, sections.missing),
                                 {"torso|arm_l", "torso|arm_r"})) {
            return false;
        }

        // The partition. I1 appends a `Missing` index to `updated`; every count
        // still sums, and only this clause fails.
        if (!mar190::expect_partition("MAR-190 V1", sections, plan.layers.size())) {
            return false;
        }

        // Each section's indices ascend. Redundant beside the ordered rows above
        // and labelled so: it names the invariant the header promises.
        for (const auto& entry : {std::make_pair("added", &sections.added),
                                  std::make_pair("updated", &sections.updated),
                                  std::make_pair("missing", &sections.missing)}) {
            if (!std::is_sorted(entry.second->begin(), entry.second->end())) {
                std::cerr << "MAR-190 V1: section '" << entry.first
                          << "' indices are not ascending.\n";
                return false;
            }
        }
    }

    // ---- V2 -- confirmation gating. ----------------------------------------
    {
        marrow::editor::PsdReimportReview confirmable;
        confirmable.plan = plan;
        if (!marrow::editor::psd_review_can_confirm(confirmable)) {
            std::cerr << "MAR-190 V2: confirmation is disabled on a clean plan carrying "
                      << plan.layers.size() << " layers.\n";
            return false;
        }

        // A plan carrying the planner's OWN error, produced by truncating a real
        // PSD rather than by hand-setting the field -- so the error text is the
        // parser's and V2 asserts a message it did not author.
        const std::filesystem::path broken_psd = scratch / "V2_broken.psd";
        {
            std::ofstream stream(broken_psd, std::ios::binary | std::ios::trunc);
            stream << "8BPS";
        }
        marrow::editor::PsdReimportPlanOptions broken_options;
        broken_options.psd_path = broken_psd;
        broken_options.staging_root = scratch / "staging_broken";
        marrow::editor::PsdReimportReview failed;
        failed.plan = marrow::editor::plan_psd_reimport(project, broken_options);
        if (failed.plan) {
            std::cerr << "MAR-190 V2: a four-byte PSD planned successfully.\n";
            return false;
        }
        if (marrow::editor::psd_review_can_confirm(failed)) {
            std::cerr << "MAR-190 V2: confirmation is enabled on a plan carrying error '"
                      << failed.plan.error->message << "'.\n";
            return false;
        }

        // The empty plan: nothing to do in any of the three categories.
        marrow::editor::PsdReimportReview empty;
        if (marrow::editor::psd_review_can_confirm(empty)) {
            std::cerr << "MAR-190 V2: confirmation is enabled on a plan with no layers in "
                         "any section.\n";
            return false;
        }
    }

    // ---- D1 -- AC2's DEFAULT: nothing is forgotten unless it is ticked. -----
    //
    // The full `(identity, preserve)` list, ordered, never a count of `false`s.
    // A count reads "seven layers, zero forgotten" as consistent whether the
    // zero is the right zero or an accident, and I4 (seeding the forget set from
    // the `Missing` section) produces a plan whose count clause still sums.
    std::vector<std::string> d1_preserve_rows;
    {
        marrow::editor::PsdReimportReview review;
        review.plan = plan;

        if (!mar188::expect_rows("MAR-190 D1 (the forget set, whole)",
                                 marrow::editor::chosen_psd_deletions(review), {})) {
            return false;
        }

        const marrow::editor::PsdReimportPlan derived =
            marrow::editor::build_psd_commit_plan(review);
        for (const marrow::editor::PsdPlannedLayer& layer : derived.layers) {
            d1_preserve_rows.push_back(
                layer.identity + " preserve=" + (layer.preserve ? "true" : "false"));
        }
        const std::vector<std::string> expected = {
            "fx|glow preserve=true",       "fx|halo preserve=true",
            "shadow preserve=true",        "torso|arm_l preserve=true",
            "torso|arm_r preserve=true",   "torso|body preserve=true",
            "torso|hand_r preserve=true",
        };
        if (!mar188::expect_rows("MAR-190 D1 (derived preserve list, ordered)",
                                 d1_preserve_rows, expected)) {
            return false;
        }

        // The by-value property is asserted in D2, NOT here. With an empty
        // forget set this derivation sets nothing to `false`, so a clause here
        // saying "the review was not mutated" cannot fail whatever the
        // implementation does -- a gate that passes on unchanged code. Measured:
        // an inversion deriving through a `const_cast` on `review.plan` left
        // this case green.
    }

    // ---- D2 -- AC2's OPT-IN: exactly the ticked identity, and nothing else. --
    {
        marrow::editor::PsdReimportReview review;
        review.plan = plan;
        marrow::editor::set_psd_review_deletion(&review, "torso|arm_l", true);

        if (!mar188::expect_rows("MAR-190 D2 (the forget set, whole)",
                                 marrow::editor::chosen_psd_deletions(review),
                                 {"torso|arm_l"})) {
            return false;
        }

        const marrow::editor::PsdReimportPlan derived =
            marrow::editor::build_psd_commit_plan(review);
        std::vector<std::string> rows;
        for (const marrow::editor::PsdPlannedLayer& layer : derived.layers) {
            rows.push_back(
                layer.identity + " preserve=" + (layer.preserve ? "true" : "false"));
        }
        const std::vector<std::string> expected = {
            "fx|glow preserve=true",       "fx|halo preserve=true",
            "shadow preserve=true",        "torso|arm_l preserve=false",
            "torso|arm_r preserve=true",   "torso|body preserve=true",
            "torso|hand_r preserve=true",
        };
        if (!mar188::expect_rows("MAR-190 D2 (derived preserve list, ordered)", rows,
                                 expected)) {
            return false;
        }

        // The list differs from D1's in EXACTLY one position. Asserted as a
        // positional diff rather than as "one false appears": the latter passes
        // if the wrong layer is the one forgotten.
        std::vector<std::size_t> differing;
        for (std::size_t index = 0; index < rows.size() && index < d1_preserve_rows.size();
             ++index) {
            if (rows[index] != d1_preserve_rows[index]) {
                differing.push_back(index);
            }
        }
        if (differing.size() != 1U || rows[differing.front()] != "torso|arm_l preserve=false") {
            std::cerr << "MAR-190 D2: the derived plan must differ from D1's in exactly one "
                         "position, at 'torso|arm_l'; it differs in "
                      << differing.size() << " position(s):";
            for (const std::size_t index : differing) {
                std::cerr << " [" << index << "] " << d1_preserve_rows[index] << " -> "
                          << rows[index];
            }
            std::cerr << ".\n";
            return false;
        }

        // By VALUE, asserted where it can actually fail. D2's forget set is
        // non-empty, so an in-place derivation writes `preserve=false` into the
        // REVIEW's plan and this clause sees it. That is what keeps I3 (Task 5)
        // a real inversion instead of two names for one object.
        for (const marrow::editor::PsdPlannedLayer& layer : review.plan.layers) {
            if (!layer.preserve) {
                std::cerr << "MAR-190 D2: deriving the commit plan mutated the REVIEW's "
                             "own plan; layer '"
                          << layer.identity
                          << "' now has preserve=false. The derivation must return a copy.\n";
                return false;
            }
        }

        // Unticking restores the default exactly -- the set is the whole state,
        // so there is nowhere for a stale `false` to survive.
        marrow::editor::set_psd_review_deletion(&review, "torso|arm_l", false);
        if (!mar188::expect_rows("MAR-190 D2 (unticked, the forget set, whole)",
                                 marrow::editor::chosen_psd_deletions(review), {})) {
            return false;
        }
    }

    // ---- V3 -- STALE, PSD side. The PSD changed while the modal was open. ---
    {
        mar189::Scenario scenario;
        if (!mar189::open_scenario(scratch, "v3", mar190::base_tree(),
                                   mar190::candidate_tree(), &scenario) ||
            !mar189::plan_scenario(&scenario, "MAR-190 V3")) {
            return false;
        }

        marrow::editor::PsdReimportReview review;
        review.plan = scenario.plan;
        review.plan_digest = marrow::editor::psd_review_plan_digest(scenario.plan);
        review.staging_root = scenario.staging_root;
        review.source_path = scenario.candidate_psd;

        // The PSD gains a layer AFTER the review was taken. The reviewed plan is
        // now a description of a file that no longer exists in that form.
        std::vector<mar188::SynthLayer> changed = mar190::candidate_tree();
        changed.push_back({{"fx"}, "spark", 20, 2, 5, 5, 7U, 8U, 9U});
        if (!mar188::write_synthetic_psd(scenario.candidate_psd, 64, 64, changed)) {
            std::cerr << "MAR-190 V3: the synthesiser could not rewrite the candidate.\n";
            return false;
        }

        // The baseline is captured AFTER every deliberate setup step, so the
        // case cannot fail on its own fixture.
        const mar190::NoOpWitness before = mar190::capture_no_op(scenario.session);

        marrow::editor::PsdReimportReviewOptions apply_options;
        apply_options.project_path = scenario.project_path;
        apply_options.restage_root = scratch / "v3_restage";
        const marrow::editor::PsdReviewApplyResult applied =
            marrow::editor::apply_psd_reimport_review(scenario.session, review, apply_options);

        if (applied.outcome != marrow::editor::PsdReviewOutcome::Stale) {
            std::cerr << "MAR-190 V3: expected outcome Stale, got "
                      << marrow::editor::psd_review_outcome_text(applied.outcome)
                      << " (error '" << applied.error << "').\n";
            return false;
        }
        // The MESSAGE, not `!result`. An unrelated refusal would satisfy a bare
        // negative check while proving nothing about staleness.
        if (applied.error.find("no longer matches") == std::string::npos) {
            std::cerr << "MAR-190 V3: the error must say the reviewed plan no longer "
                         "matches; got '"
                      << applied.error << "'.\n";
            return false;
        }
        if (applied.commit.has_value()) {
            std::cerr << "MAR-190 V3: a commit was attempted on a stale review.\n";
            return false;
        }
        if (!mar190::expect_reimport_no_op(scenario.session, before, "MAR-190 V3")) {
            return false;
        }
        // Step 5 on a failure path. Staging is not a target, so NO byte-map
        // clause anywhere can see a leaked restage tree -- this is its only
        // detector, which is what makes it non-decorative.
        if (std::filesystem::exists(apply_options.restage_root)) {
            std::cerr << "MAR-190 V3: the restage root "
                      << std::filesystem::absolute(apply_options.restage_root).generic_string()
                      << " still exists after a stale review.\n";
            return false;
        }
    }

    // ---- V4 -- PLANNING FAILURE. The PSD became unreadable. -----------------
    {
        mar189::Scenario scenario;
        if (!mar189::open_scenario(scratch, "v4", mar190::base_tree(),
                                   mar190::candidate_tree(), &scenario) ||
            !mar189::plan_scenario(&scenario, "MAR-190 V4")) {
            return false;
        }

        marrow::editor::PsdReimportReview review;
        review.plan = scenario.plan;
        review.plan_digest = marrow::editor::psd_review_plan_digest(scenario.plan);
        review.staging_root = scenario.staging_root;
        review.source_path = scenario.candidate_psd;

        {
            std::ofstream truncated(
                scenario.candidate_psd, std::ios::binary | std::ios::trunc);
            truncated << "8BPS";
        }

        const mar190::NoOpWitness before = mar190::capture_no_op(scenario.session);

        marrow::editor::PsdReimportReviewOptions apply_options;
        apply_options.project_path = scenario.project_path;
        apply_options.restage_root = scratch / "v4_restage";
        const marrow::editor::PsdReviewApplyResult applied =
            marrow::editor::apply_psd_reimport_review(scenario.session, review, apply_options);

        if (applied.outcome != marrow::editor::PsdReviewOutcome::PlanFailed) {
            std::cerr << "MAR-190 V4: expected outcome PlanFailed, got "
                      << marrow::editor::psd_review_outcome_text(applied.outcome)
                      << " (error '" << applied.error << "').\n";
            return false;
        }
        // The PLANNER's own rejection text, which this story does not author.
        if (applied.error.find("PSD") == std::string::npos) {
            std::cerr << "MAR-190 V4: the error must carry the planner's own rejection; "
                         "got '"
                      << applied.error << "'.\n";
            return false;
        }
        if (applied.commit.has_value()) {
            std::cerr << "MAR-190 V4: a commit was attempted after planning failed.\n";
            return false;
        }
        if (!mar190::expect_reimport_no_op(scenario.session, before, "MAR-190 V4")) {
            return false;
        }
        if (std::filesystem::exists(apply_options.restage_root)) {
            std::cerr << "MAR-190 V4: the restage root "
                      << std::filesystem::absolute(apply_options.restage_root).generic_string()
                      << " still exists after a planning failure.\n";
            return false;
        }
    }

    // ---- D1/D2 commit halves, V5, V7 -- one successful commit each. ---------
    //
    // AC2's observable is the stored provenance row list, NOT the committed
    // skeleton's slots. A `Missing` layer's slot is removed by the reimport in
    // EITHER direction, because `build_skeleton_document` assigns `slots`
    // wholesale from the newly parsed PSD and erases `skins`
    // (`psd_import.cpp:1040-1041`); MAR-189's `prune_unpreserved` says so in its
    // own comment. `preserve` governs whether the project keeps REMEMBERING the
    // layer. Asserting a preserved slot survives would fail on correct code.
    const auto commit_case = [&](const char* label,
                                 const std::vector<std::string>& forget,
                                 const std::vector<std::string>& expected_provenance,
                                 std::vector<std::string>* slots_out) {
        mar189::Scenario scenario;
        if (!mar189::open_scenario(scratch, label, mar190::base_tree(),
                                   mar190::candidate_tree(), &scenario) ||
            !mar189::plan_scenario(&scenario, label)) {
            return false;
        }
        marrow::editor::PsdReimportReview review;
        review.plan = scenario.plan;
        review.plan_digest = marrow::editor::psd_review_plan_digest(scenario.plan);
        review.staging_root = scenario.staging_root;
        review.source_path = scenario.candidate_psd;
        for (const std::string& identity : forget) {
            marrow::editor::set_psd_review_deletion(&review, identity, true);
        }

        const std::size_t undo_before = scenario.session.undo_count();
        marrow::editor::PsdReimportReviewOptions apply_options;
        apply_options.project_path = scenario.project_path;
        apply_options.restage_root = scratch / (std::string(label) + "_restage");
        const marrow::editor::PsdReviewApplyResult applied =
            marrow::editor::apply_psd_reimport_review(scenario.session, review, apply_options);

        if (applied.outcome != marrow::editor::PsdReviewOutcome::Committed) {
            std::cerr << label << ": expected Committed, got "
                      << marrow::editor::psd_review_outcome_text(applied.outcome)
                      << " (error '" << applied.error << "').\n";
            return false;
        }
        if (!applied.error.empty()) {
            std::cerr << label << ": a committed reimport must carry no error; got '"
                      << applied.error << "'.\n";
            return false;
        }
        if (!applied.commit.has_value() || !applied.commit->ok) {
            std::cerr << label << ": the commit ledger is missing or not ok.\n";
            return false;
        }
        // The ledger ENDS where the enum ends. A containment check passes on a
        // commit that carried on past the step it reported.
        if (!mar189::expect_steps(
                mar189::step_names(applied.commit->steps_executed),
                mar189::step_names(std::vector<marrow::editor::PsdCommitStep>(
                    marrow::editor::kAllCommitSteps.begin(),
                    marrow::editor::kAllCommitSteps.end())),
                label)) {
            return false;
        }
        if (applied.commit->rolled_back || !applied.commit->steps_rolled_back.empty()) {
            std::cerr << label << ": a successful commit rolled something back.\n";
            return false;
        }
        // AC2, the whole list, ordered.
        if (!mar188::expect_rows(
                std::string(label) + " (stored provenance, whole ordered list)",
                mar190::provenance_rows(*scenario.session.project()),
                expected_provenance)) {
            return false;
        }
        // V7 -- provenance is refreshed, TYPED. `image_file` is a bare name for
        // every entry (a stored path could only restate `layers_directory` and
        // would then have two spellings to keep in agreement), and both stored
        // paths are project-RELATIVE so the bundle survives being moved.
        {
            const marrow::editor::PsdImportProvenance& stored =
                *scenario.session.project()->editor_metadata.import_sources->psd;
            for (const marrow::editor::PsdLayerProvenance& layer : stored.layers) {
                if (layer.image_file != std::filesystem::path(layer.image_file)
                                            .filename()
                                            .generic_string() ||
                    layer.image_file.find('/') != std::string::npos) {
                    std::cerr << label << ": provenance image_file '" << layer.image_file
                              << "' is not a bare file name.\n";
                    return false;
                }
            }
            // `layers_directory` lives inside the bundle and is always relative,
            // which is what lets the bundle be moved.
            if (stored.layers_directory.is_absolute()) {
                std::cerr << label << ": layers_directory must be project-relative; got '"
                          << stored.layers_directory.generic_string() << "'.\n";
                return false;
            }
            // `source_path` is NOT asserted relative. `provenance_from_plan`
            // relativizes against the project file, and this fixture's PSD sits
            // outside the project directory, where no relative form exists that is
            // not a `..` chain -- so an is_relative() clause here would fail on
            // correct code, which it did when first written. The property that
            // actually matters is that the stored path still FINDS the PSD.
            const std::filesystem::path resolved =
                scenario.session.project()->resolve_path(stored.source_path);
            std::error_code same_error;
            if (!std::filesystem::equivalent(resolved, scenario.candidate_psd, same_error)) {
                std::cerr << label << ": stored source_path '"
                          << stored.source_path.generic_string() << "' resolves to '"
                          << resolved.generic_string() << "', not to the reimported PSD '"
                          << scenario.candidate_psd.generic_string() << "'.\n";
                return false;
            }
        }

        // AC5's history half: exactly one new entry, and redo cleared.
        if (scenario.session.undo_count() != undo_before + 1U) {
            std::cerr << label << ": undo_count() is " << scenario.session.undo_count()
                      << ", expected " << (undo_before + 1U)
                      << " (the provenance update is one entry).\n";
            return false;
        }
        if (scenario.session.redo_count() != 0U) {
            std::cerr << label << ": redo_count() is " << scenario.session.redo_count()
                      << ", expected 0.\n";
            return false;
        }
        if (std::filesystem::exists(apply_options.restage_root)) {
            std::cerr << label << ": the restage root "
                      << std::filesystem::absolute(apply_options.restage_root).generic_string()
                      << " still exists after a successful commit.\n";
            return false;
        }
        if (slots_out != nullptr) {
            *slots_out = mar190::committed_slot_names(
                scenario.session.project()->resolved_skeleton_path());
        }
        return true;
    };

    // The bone is the enclosing GROUP name (`root` for an ungrouped layer), not the
    // layer's -- MAR-188's importer creates one bone per folder. Measured, not
    // assumed; these tuples are the importer's output and this story does not
    // define them. What this story DOES define, and what these cases discriminate
    // on, is which rows survive the commit.
    // D1's commit half: nothing ticked, so BOTH `Missing` layers keep their rows,
    // each carrying its CURRENT (pre-reimport) mapping.
    std::vector<std::string> d1_slots;
    {
        const std::vector<std::string> expected = {
            "fx|glow -> glow,glow,fx,glow.png",
            "fx|halo -> halo,halo,fx,halo.png",
            "shadow -> shadow,shadow,root,shadow.png",
            "torso|arm_l -> arm_l,arm_l,torso,arm_l.png",
            "torso|arm_r -> arm_r,arm_r,torso,arm_r.png",
            "torso|body -> body,body,torso,body.png",
            "torso|hand_r -> hand_r,hand_r,torso,hand_r.png",
        };
        if (!commit_case("MAR-190 D1c", {}, expected, &d1_slots)) {
            return false;
        }
    }

    // D2's commit half + V5: exactly the ticked identity loses its row, and the
    // other `Missing` layer keeps its own. The set difference from D1c is ONE row.
    std::vector<std::string> d2_slots;
    {
        const std::vector<std::string> expected = {
            "fx|glow -> glow,glow,fx,glow.png",
            "fx|halo -> halo,halo,fx,halo.png",
            "shadow -> shadow,shadow,root,shadow.png",
            "torso|arm_r -> arm_r,arm_r,torso,arm_r.png",
            "torso|body -> body,body,torso,body.png",
            "torso|hand_r -> hand_r,hand_r,torso,hand_r.png",
        };
        if (!commit_case("MAR-190 D2c/V5", {"torso|arm_l"}, expected, &d2_slots)) {
            return false;
        }
        if (d1_slots.size() != d2_slots.size() || d1_slots != d2_slots) {
            std::cerr << "MAR-190 V5: the committed SLOT set must be identical whether a "
                         "missing layer was forgotten or preserved -- the reimport removes "
                         "it either way. D1c had " << d1_slots.size() << " slots, D2c/V5 had "
                      << d2_slots.size() << ".\n";
            return false;
        }
        // And the slots are the CANDIDATE's, with neither `Missing` layer present
        // in either run. This is the clause that would have been written the
        // wrong way round.
        const std::vector<std::string> expected_slots = {
            "body", "glow", "halo", "hand_r", "shadow",
        };
        if (!mar188::expect_rows("MAR-190 V5 (committed slot names, sorted)", d2_slots,
                                 expected_slots)) {
            return false;
        }
    }

    // ---- V9 -- a PRE-EXISTING redo stack survives a failed reimport. --------
    //
    // This is the case that proves AC3 was not fixed by breaking AC5. The
    // rollback has to drop the entry IT pushed and nothing else; the obvious
    // remedy -- `clear_history()` -- also removes every redo entry the user had
    // before they ever opened the reimport, which is AC5's "preserving existing
    // unsaved overlays and undo/redo history" broken in the course of repairing
    // AC3. Without this case both implementations pass V8 identically.
    {
        using marrow::editor::PsdCommitStep;
        mar189::Scenario scenario;
        if (!mar189::open_scenario(scratch, "v9", mar190::base_tree(),
                                   mar190::candidate_tree(), &scenario) ||
            !mar189::plan_scenario(&scenario, "MAR-190 V9")) {
            return false;
        }

        // Give the user a real redo stack: make an edit, then undo it. This is a
        // NORMAL user undo, so the entry is legitimately redoable and must stay so.
        {
            // An RAII transaction: it commits when the scope closes.
            auto seed = scenario.session.begin_edit(
                {marrow::editor::EditKind::EditProperty,
                 "mar190 user edit",
                 {},
                 false,
                 marrow::editor::EditImpact::Project});
            if (!seed) {
                std::cerr << "MAR-190 V9: could not begin the seed edit.\n";
                return false;
            }
            marrow::editor::IkConstraintEdit edit;
            edit.name = "mar190_user_edit";
            edit.bone_names = {"torso"};
            edit.target_bone_name = "root";
            seed.project()->ik_constraint_edits.push_back(std::move(edit));
            // EXPLICIT commit. The destructor CANCELS an uncommitted transaction,
            // so relying on scope exit produced no history entry at all and this
            // case silently had nothing to protect.
            const marrow::editor::SessionResult sealed = seed.commit();
            if (!sealed) {
                std::cerr << "MAR-190 V9: could not commit the seed edit: "
                          << sealed.error->format() << '\n';
                return false;
            }
        }
        if (scenario.session.undo_count() == 0U) {
            std::cerr << "MAR-190 V9: the seed edit produced no undo entry.\n";
            return false;
        }
        {
            const marrow::editor::SessionResult undone = scenario.session.undo();
            if (!undone) {
                std::cerr << "MAR-190 V9: could not undo the seed edit: "
                          << undone.error->format() << '\n';
                return false;
            }
        }
        if (scenario.session.redo_count() != 1U) {
            std::cerr << "MAR-190 V9: the fixture needs a redo stack of exactly 1; got "
                      << scenario.session.redo_count()
                      << ". Without one this case cannot fail.\n";
            return false;
        }
        const std::string redo_label_before(scenario.session.redo_label());

        marrow::editor::PsdReimportReview review;
        review.plan = scenario.plan;
        review.plan_digest = marrow::editor::psd_review_plan_digest(scenario.plan);
        review.staging_root = scenario.staging_root;
        review.source_path = scenario.candidate_psd;

        const mar190::NoOpWitness before = mar190::capture_no_op(scenario.session);

        marrow::editor::PsdReimportReviewOptions apply_options;
        apply_options.project_path = scenario.project_path;
        apply_options.restage_root = scratch / "v9_restage";

        // Fail AFTER `UpdateProvenance`, the one arm whose rollback touches history.
        marrow::editor::detail::set_psd_commit_failpoint_for_testing(
            mar189::fail_after(PsdCommitStep::UpdateProvenance));
        const marrow::editor::PsdReviewApplyResult applied =
            marrow::editor::apply_psd_reimport_review(scenario.session, review, apply_options);
        marrow::editor::detail::set_psd_commit_failpoint_for_testing({});

        if (applied.outcome != marrow::editor::PsdReviewOutcome::CommitFailed) {
            std::cerr << "MAR-190 V9: expected CommitFailed, got "
                      << marrow::editor::psd_review_outcome_text(applied.outcome) << ".\n";
            return false;
        }
        // The user's OWN redo entry is still there, by depth AND by label -- a
        // depth of 1 would also pass if the rollback had swapped its own entry in.
        if (scenario.session.redo_count() != 1U) {
            std::cerr << "MAR-190 V9: the user's redo stack was " << 1U << " before the "
                         "failed reimport and is "
                      << scenario.session.redo_count()
                      << " after. A rollback must discard only the entry it pushed.\n";
            return false;
        }
        if (std::string(scenario.session.redo_label()) != redo_label_before) {
            std::cerr << "MAR-190 V9: the top of the redo stack is now '"
                      << scenario.session.redo_label() << "', was '" << redo_label_before
                      << "' -- the rollback replaced the user's entry with its own.\n";
            return false;
        }
        // And redoing it still works, which a depth check alone cannot show.
        const marrow::editor::SessionResult redone = scenario.session.redo();
        if (!redone) {
            std::cerr << "MAR-190 V9: the user's redo entry survived in the count but "
                         "could not be applied: "
                      << redone.error->format() << '\n';
            return false;
        }
        (void)before;
    }

    // ---- V6 -- AC5's overlay preservation, by FULL IDENTITY. ----------------
    //
    // Not spot checks. Serialize before, commit, then overwrite the AFTER
    // project's `import_sources` with the BEFORE value and re-serialize: the two
    // strings must be byte-equal. Exactly one field is excused, by name, and
    // everything else in `ProjectData` is asserted -- a dropped animation edit,
    // curve, constraint, inherit or editor overlay fails it without anyone having
    // to have thought of that field.
    //
    // Both strings are produced IN MEMORY from live `ProjectData`. `AGENTS.md`:
    // `serialize_project` is not bit-exact for doubles needing 17 significant
    // digits, so a file round trip would drift under the comparison.
    {
        mar189::Scenario scenario;
        if (!mar189::open_scenario(
                scratch, "v6", mar190::base_tree(), mar190::candidate_tree(), &scenario,
                [](marrow::editor::ProjectData* project) {
                    // `torso` and `root` are bones BOTH trees produce, so this
                    // constraint still resolves against the staged bundle. R2(b)
                    // measured that one naming a dropped bone is refused outright,
                    // which would test the refusal rather than the preservation.
                    marrow::editor::IkConstraintEdit edit;
                    edit.name = "mar190_overlay";
                    edit.bone_names = {"torso"};
                    edit.target_bone_name = "root";
                    project->ik_constraint_edits.push_back(std::move(edit));
                }) ||
            !mar189::plan_scenario(&scenario, "MAR-190 V6")) {
            return false;
        }

        marrow::editor::PsdReimportReview review;
        review.plan = scenario.plan;
        review.plan_digest = marrow::editor::psd_review_plan_digest(scenario.plan);
        review.staging_root = scenario.staging_root;
        review.source_path = scenario.candidate_psd;

        const marrow::editor::ProjectData before_project = *scenario.session.project();
        const std::string before_text = marrow::editor::serialize_project(before_project);
        const std::size_t undo_before = scenario.session.undo_count();

        marrow::editor::PsdReimportReviewOptions apply_options;
        apply_options.project_path = scenario.project_path;
        apply_options.restage_root = scratch / "v6_restage";
        const marrow::editor::PsdReviewApplyResult applied =
            marrow::editor::apply_psd_reimport_review(scenario.session, review, apply_options);
        if (applied.outcome != marrow::editor::PsdReviewOutcome::Committed) {
            std::cerr << "MAR-190 V6: expected Committed, got "
                      << marrow::editor::psd_review_outcome_text(applied.outcome)
                      << " (error '" << applied.error << "').\n";
            return false;
        }

        // The one excused field, named. Everything else must be byte-identical.
        marrow::editor::ProjectData rebased = *scenario.session.project();
        rebased.editor_metadata.import_sources = before_project.editor_metadata.import_sources;
        const std::string rebased_text = marrow::editor::serialize_project(rebased);
        if (rebased_text != before_text) {
            std::size_t offset = 0;
            const std::size_t shared = std::min(rebased_text.size(), before_text.size());
            while (offset < shared && rebased_text[offset] == before_text[offset]) {
                ++offset;
            }
            std::cerr << "MAR-190 V6: the commit changed something other than "
                         "import_sources; the projects first differ at offset "
                      << offset << " (before " << before_text.size() << " bytes, after "
                      << rebased_text.size() << ").\n";
            return false;
        }
        // The overlay is still there BY NAME as well. The clause above would catch
        // its loss, but this says which thing was lost when it fails.
        if (scenario.session.project()->ik_constraint_edits.size() !=
                before_project.ik_constraint_edits.size() ||
            scenario.session.project()->ik_constraint_edits.empty() ||
            scenario.session.project()->ik_constraint_edits.front().name !=
                "mar190_overlay") {
            std::cerr << "MAR-190 V6: the seeded IK constraint overlay did not survive.\n";
            return false;
        }
        if (scenario.session.undo_count() != undo_before + 1U ||
            scenario.session.redo_count() != 0U) {
            std::cerr << "MAR-190 V6: undo_count() is " << scenario.session.undo_count()
                      << " (expected " << (undo_before + 1U) << ") and redo_count() is "
                      << scenario.session.redo_count() << " (expected 0).\n";
            return false;
        }
    }

    // ---- V8 -- commit failure, one arm per step the seam can reach. ---------
    //
    // Scope: this asserts how the REVIEW LAYER maps a commit failure, not how the
    // commit behaves -- MAR-189's R3 owns the commit body and sweeps it far more
    // thoroughly than a second copy here would. What is MAR-190's to prove is
    // that every failure becomes `CommitFailed` carrying the step's own message,
    // that the ledger ENDS where the failure was injected, and that AC3's whole
    // invariant holds on every one of them.
    {
        using marrow::editor::PsdCommitStep;
        struct Arm {
            PsdCommitStep step;
            bool rolls_back;  ///< False only for the arm that leaves a COMPLETED reimport.
        };
        // `CleanJournal` is the fifteenth arm and the asymmetric one: the reimport
        // is committed and only the journal cleanup failed. Rolling that back
        // would destroy a finished reimport, so `commit_psd_reimport` reports
        // `ok` WITH an error and the review layer must report Committed. A uniform
        // `!ok` sweep is wrong here in the destructive direction.
        const std::vector<Arm> arms = {
            {PsdCommitStep::ValidateRequest, true},
            {PsdCommitStep::PruneUnpreserved, true},
            {PsdCommitStep::ValidateStagedBundle, true},
            {PsdCommitStep::OpenJournal, true},
            {PsdCommitStep::BackupLayers, true},
            {PsdCommitStep::BackupTexture, true},
            {PsdCommitStep::BackupAtlas, true},
            {PsdCommitStep::BackupSkeleton, true},
            {PsdCommitStep::PlaceLayers, true},
            {PsdCommitStep::PlaceTexture, true},
            {PsdCommitStep::PlaceAtlas, true},
            {PsdCommitStep::PlaceSkeleton, true},
            {PsdCommitStep::AdoptRuntimeSources, true},
            {PsdCommitStep::UpdateProvenance, true},
            {PsdCommitStep::CleanJournal, false},
        };
        // The table is checked against the ENUM by identity, never by size: a size
        // check passes on a table with the right count and the wrong members, and
        // AC6 says every step.
        {
            std::vector<std::string> table;
            for (const Arm& arm : arms) {
                table.emplace_back(marrow::editor::psd_commit_step_name(arm.step));
            }
            std::vector<std::string> all;
            for (const PsdCommitStep step : marrow::editor::kAllCommitSteps) {
                all.emplace_back(marrow::editor::psd_commit_step_name(step));
            }
            if (!mar189::expect_steps(table, all, "MAR-190 V8 (table covers the enum)")) {
                return false;
            }
        }

        bool adoption_seen = false;
        for (const Arm& arm : arms) {
            const std::string step_name = marrow::editor::psd_commit_step_name(arm.step);
            const std::string label = "MAR-190 V8[" + step_name + "]";

            mar189::Scenario scenario;
            if (!mar189::open_scenario(scratch, "v8_" + step_name, mar190::base_tree(),
                                       mar190::candidate_tree(), &scenario) ||
                !mar189::plan_scenario(&scenario, label.c_str())) {
                return false;
            }
            marrow::editor::PsdReimportReview review;
            review.plan = scenario.plan;
            review.plan_digest = marrow::editor::psd_review_plan_digest(scenario.plan);
            review.staging_root = scenario.staging_root;
            review.source_path = scenario.candidate_psd;

            const mar190::NoOpWitness before = mar190::capture_no_op(scenario.session);

            marrow::editor::PsdReimportReviewOptions apply_options;
            apply_options.project_path = scenario.project_path;
            apply_options.restage_root = scratch / ("v8_" + step_name + "_restage");

            marrow::editor::detail::set_psd_commit_failpoint_for_testing(
                mar189::fail_after(arm.step));
            const marrow::editor::PsdReviewApplyResult applied =
                marrow::editor::apply_psd_reimport_review(
                    scenario.session, review, apply_options);
            marrow::editor::detail::set_psd_commit_failpoint_for_testing({});

            if (!applied.commit.has_value()) {
                std::cerr << label << ": no commit ledger was returned.\n";
                return false;
            }
            // The ledger ENDS at the injected step. The failpoint fires AFTER the
            // step's body succeeds, so that step is recorded and must be the LAST
            // one. A containment check passes on a commit that carried on past the
            // step it reported failing at.
            if (applied.commit->steps_executed.empty() ||
                applied.commit->steps_executed.back() != arm.step) {
                std::cerr << label << ": the step ledger must END at " << step_name
                          << "; it ends at "
                          << (applied.commit->steps_executed.empty()
                                  ? "<empty>"
                                  : marrow::editor::psd_commit_step_name(
                                        applied.commit->steps_executed.back()))
                          << ".\n";
                return false;
            }

            if (!arm.rolls_back) {
                // The completed-reimport arm. The review layer must NOT report a
                // failure and must NOT be a no-op: the reimport happened.
                if (applied.outcome != marrow::editor::PsdReviewOutcome::Committed) {
                    std::cerr << label
                              << ": a failure after the reimport completed must still be "
                                 "Committed; got "
                              << marrow::editor::psd_review_outcome_text(applied.outcome)
                              << ".\n";
                    return false;
                }
                if (applied.commit->rolled_back) {
                    std::cerr << label
                              << ": a completed reimport was ROLLED BACK, which destroys "
                                 "it.\n";
                    return false;
                }
                continue;
            }

            if (applied.outcome != marrow::editor::PsdReviewOutcome::CommitFailed) {
                std::cerr << label << ": expected CommitFailed, got "
                          << marrow::editor::psd_review_outcome_text(applied.outcome)
                          << " (error '" << applied.error << "').\n";
                return false;
            }
            // The MESSAGE names the step. `!ok` alone passes under an unrelated
            // failure and would prove nothing about which arm was exercised.
            if (applied.error.find(step_name) == std::string::npos) {
                std::cerr << label << ": the error must name " << step_name << "; got '"
                          << applied.error << "'.\n";
                return false;
            }
            if (!applied.commit->rolled_back) {
                std::cerr << label << ": the commit did not report a rollback.\n";
                return false;
            }
            // The failpoint fires AFTER the step's body, so adoption has already
            // run on ITS OWN arm -- the allowance must include that arm, not just
            // the ones after it. Measured: `runtime 1->3, preview 1->3` on the
            // AdoptRuntimeSources arm, which is the rollback's re-adopt on top of
            // the adoption itself.
            const bool revisions_may_move =
                adoption_seen || arm.step == PsdCommitStep::AdoptRuntimeSources;
            const bool provenance_reverted = arm.step == PsdCommitStep::UpdateProvenance;
            if (!mar190::expect_reimport_no_op(scenario.session, before, label,
                                               revisions_may_move, provenance_reverted)) {
                return false;
            }
            if (std::filesystem::exists(apply_options.restage_root)) {
                std::cerr << label << ": the restage root still exists.\n";
                return false;
            }
            if (arm.step == PsdCommitStep::AdoptRuntimeSources) {
                adoption_seen = true;
            }
        }
    }

    std::cout << "MAR-190 V1-V2, D1-D2: the three review sections partition the plan and "
                 "preserve its ascending identity order; confirmation is refused for a plan "
                 "carrying a planner error and for a plan with nothing to do; and the "
                 "derived commit plan forgets exactly the ticked identities, defaulting to "
                 "none, without mutating the review; and a PSD that changed under an "
                 "open review, or became unreadable, is refused as Stale and PlanFailed "
                 "with the project, its runtime sources, its bytes and its history "
                 "unchanged and the restage root removed; a confirmed reimport commits the "
                 "full step ledger, keeps exactly the provenance rows that were not "
                 "forgotten, leaves every other authored field byte-identical, and adds "
                 "exactly one undo entry; and an injected failure after each of the "
                 "fifteen commit steps is reported as CommitFailed naming that step with "
                 "the ledger ending there, except after CleanJournal where the completed "
                 "reimport stands; and a redo stack the user already had survives a "
                 "failed reimport intact and still applies.\n";
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

    // Spans the whole of main's import round trip, so it is a guard rather than a
    // scoped block; every failing exit below calls keep() first.
    ScratchRoot import_root(scratch_root("marrow_psd_import_smoke"));
    const std::filesystem::path temp_root = import_root.path();
    std::error_code error;
    std::filesystem::remove_all(temp_root, error);
    error.clear();
    std::filesystem::create_directories(temp_root, error);
    if (error) {
        std::cerr << error.message() << '\n';
        import_root.keep();
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
        import_root.keep();
        return 1;
    }
    // Q0 runs BEFORE every other MAR-188 case so a synthesiser regression is
    // attributed to the gate rather than to whichever case happens to notice.
    {
        ScratchRoot root(scratch_root("mar188_q0"));
        if (!validate_mar188_q0(options.initial_psd, root.path())) {
            root.keep();
            return 1;
        }
    }
    {
        ScratchRoot root(scratch_root("mar188_plan"));
        if (!validate_mar188_reimport_planning(root.path())) {
            root.keep();
            return 1;
        }
    }
    {
        ScratchRoot root(scratch_root("mar189_naming"));
        if (!validate_mar189_staged_naming(root.path())) {
            root.keep();
            return 1;
        }
    }
    {
        // One guard for both suites: they share a root deliberately, so disposal
        // must not happen between them.
        ScratchRoot root(scratch_root("mar189_commit"));
        if (!validate_mar189_reimport_commit(root.path()) ||
            !validate_mar189_commit_rollback(root.path())) {
            root.keep();
            return 1;
        }
    }
    // Under `/tmp`, deliberately, and NOT `temp_directory_path()`. On macOS the
    // latter is `$TMPDIR` (`/var/folders/...`), which `agent_path_allowed`
    // (`agent_dispatch.cpp:626-629`) does not whitelist -- so an A-case sited there
    // is refused as a forbidden input path before it can test anything. The two
    // sets are disjoint, and the safety gate accepts both.
    {
        // MAR-190. Owned like every other root here: removed on success, KEPT on
        // failure, because the bundle a failing case built is the thing someone
        // will want to look at.
        ScratchRoot root(scratch_root("mar190_review"));
        if (!validate_mar190_reimport_review(root.path())) {
            root.keep();
            return 1;
        }
    }
    {
        ScratchRoot root(agent_scratch_root());
        if (!validate_mar189_agent_operation(root.path())) {
            root.keep();
            return 1;
        }
    }

    if (!validate_initial_import(import_result, skeleton_path, atlas_path)) {
        import_root.keep();
        return 1;
    }

    if (!patch_animation_for_reimport(skeleton_path)) {
        import_root.keep();
        return 1;
    }

    import_options.psd_path = options.reimport_psd;
    import_options.existing_skeleton_path = skeleton_path;
    const auto reimport_result = marrow::editor::import_psd_to_runtime_bundle(import_options);
    if (!reimport_result) {
        std::cerr << reimport_result.error->format() << '\n';
        import_root.keep();
        return 1;
    }
    if (!validate_reimport(skeleton_path, atlas_path)) {
        import_root.keep();
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
