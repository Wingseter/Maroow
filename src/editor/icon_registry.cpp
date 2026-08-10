#include "icon_registry.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "image_io.hpp"
#include "sokol_gfx.h"
#define SOKOL_IMGUI_NO_SOKOL_APP
#include "sokol_imgui.h"

namespace marrow::editor {

namespace {

struct IconEntry {
    Icon icon;
    std::string_view stem;
};

constexpr std::array<IconEntry, static_cast<std::size_t>(Icon::Count)> kIconTable{{
    {Icon::Play, "play"},
    {Icon::Pause, "pause"},
    {Icon::Stop, "stop"},
    {Icon::Rewind, "rewind"},
    {Icon::PrevKey, "prev_key"},
    {Icon::NextKey, "next_key"},
    {Icon::AddKey, "add_key"},
    {Icon::RemoveKey, "remove_key"},
    {Icon::Loop, "loop"},

    {Icon::OnionSkin, "onion_skin"},
    {Icon::MeshWire, "mesh_wire"},
    {Icon::Bbox, "bbox"},
    {Icon::BoneToggle, "bone_toggle"},
    {Icon::PerfHud, "perf_hud"},
    {Icon::Eye, "eye"},

    {Icon::WeightBrush, "weight_brush"},
    {Icon::WeightErase, "weight_erase"},
    {Icon::WeightSmooth, "weight_smooth"},

    {Icon::ZoomFit, "zoom_fit"},
    {Icon::ZoomOne, "zoom_one"},

    {Icon::Save, "save"},
    {Icon::Export, "export"},
    {Icon::Reload, "reload"},
    {Icon::Undo, "undo"},
    {Icon::Redo, "redo"},
    {Icon::MoveUp, "move_up"},
    {Icon::MoveDown, "move_down"},

    {Icon::NodeBone, "node_bone"},
    {Icon::NodeSlot, "node_slot"},
    {Icon::NodeSkin, "node_skin"},
    {Icon::NodeAnim, "node_anim"},

    {Icon::AttRegion, "att_region"},
    {Icon::AttMesh, "att_mesh"},
    {Icon::AttLinked, "att_linked"},
    {Icon::AttPoint, "att_point"},
    {Icon::AttBbox, "att_bbox"},
    {Icon::AttClip, "att_clip"},
    {Icon::AttPath, "att_path"},

    {Icon::ConstraintIk, "constraint_ik"},
    {Icon::ConstraintPath, "constraint_path"},
    {Icon::ConstraintXform, "constraint_xform"},
    {Icon::ConstraintPhysics, "constraint_physics"},

    {Icon::PropRotate, "prop_rotate"},
    {Icon::PropTranslate, "prop_translate"},
    {Icon::PropScale, "prop_scale"},
    {Icon::PropShear, "prop_shear"},
    {Icon::PropColor, "prop_color"},
    {Icon::PropOrder, "prop_order"},
    {Icon::PropEvent, "prop_event"},

    {Icon::StatusWarn, "status_warn"},
    {Icon::StatusError, "status_error"},
}};

std::optional<std::pair<sg_image, sg_view>> upload_rgba_texture(
    const std::vector<std::uint8_t>& rgba8,
    int width,
    int height) {
    sg_image_desc image_desc{};
    image_desc.width = width;
    image_desc.height = height;
    image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    image_desc.data.mip_levels[0] = {rgba8.data(), rgba8.size()};
    image_desc.label = "marrow-editor-icon";
    const sg_image image = sg_make_image(&image_desc);
    if (sg_query_image_state(image) != SG_RESOURCESTATE_VALID) {
        if (image.id != SG_INVALID_ID) {
            sg_destroy_image(image);
        }
        return std::nullopt;
    }

    sg_view_desc view_desc{};
    view_desc.texture.image = image;
    view_desc.label = "marrow-editor-icon-view";
    const sg_view view = sg_make_view(&view_desc);
    if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID) {
        if (view.id != SG_INVALID_ID) {
            sg_destroy_view(view);
        }
        sg_destroy_image(image);
        return std::nullopt;
    }
    return std::pair<sg_image, sg_view>{image, view};
}

} // namespace

IconRegistry::~IconRegistry() {
    unload_all();
}

int IconRegistry::load_all(const std::filesystem::path& root) {
    unload_all();

    if (!sg_isvalid()) {
        std::fprintf(stderr, "[icon_registry] sokol_gfx is not initialized\n");
        return 0;
    }
    sg_sampler_desc sampler_desc{};
    sampler_desc.min_filter = SG_FILTER_LINEAR;
    sampler_desc.mag_filter = SG_FILTER_LINEAR;
    sampler_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler_desc.label = "marrow-editor-icon-sampler";
    sampler_ = sg_make_sampler(&sampler_desc);
    if (sg_query_sampler_state(sampler_) != SG_RESOURCESTATE_VALID) {
        sampler_ = {};
        return 0;
    }

    int loaded = 0;
    for (const auto& entry : kIconTable) {
        const std::filesystem::path path = root / (std::string(entry.stem) + ".png");
        std::vector<std::uint8_t> rgba8;
        int width = 0;
        int height = 0;
        if (auto error = detail::load_png_rgba8_raw(path, &rgba8, &width, &height)) {
            std::fprintf(
                stderr,
                "[icon_registry] failed to load %s: %s\n",
                path.string().c_str(),
                error->c_str());
            continue;
        }
        Slot& slot = slots_[static_cast<std::size_t>(entry.icon)];
        const auto texture = upload_rgba_texture(rgba8, width, height);
        if (!texture.has_value()) {
            std::fprintf(
                stderr,
                "[icon_registry] failed to create Sokol resources for %s\n",
                path.string().c_str());
            continue;
        }
        slot.image = texture->first;
        slot.view = texture->second;
        slot.imgui_texture_id = simgui_imtextureid_with_sampler(slot.view, sampler_);
        slot.width = width;
        slot.height = height;
        ++loaded;
    }
    all_loaded_ = loaded == static_cast<int>(Icon::Count);
    return loaded;
}

void IconRegistry::unload_all() {
    for (Slot& slot : slots_) {
        if (sg_isvalid()) {
            if (slot.view.id != SG_INVALID_ID) {
                sg_destroy_view(slot.view);
            }
            if (slot.image.id != SG_INVALID_ID) {
                sg_destroy_image(slot.image);
            }
        }
        slot = {};
    }
    if (sg_isvalid() && sampler_.id != SG_INVALID_ID) {
        sg_destroy_sampler(sampler_);
    }
    sampler_ = {};
    all_loaded_ = false;
}

ImTextureID IconRegistry::get(Icon icon) const {
    const std::size_t index = static_cast<std::size_t>(icon);
    if (index >= slots_.size()) {
        return 0;
    }
    return static_cast<ImTextureID>(slots_[index].imgui_texture_id);
}

ImVec2 IconRegistry::size(Icon icon) const {
    const std::size_t index = static_cast<std::size_t>(icon);
    if (index >= slots_.size()) {
        return ImVec2(0.0f, 0.0f);
    }
    return ImVec2(
        static_cast<float>(slots_[index].width),
        static_cast<float>(slots_[index].height));
}

bool IconRegistry::all_loaded() const {
    return all_loaded_;
}

std::string_view icon_filename_stem(Icon icon) {
    // Look up by the row's declared icon rather than by position, so an
    // out-of-order kIconTable row cannot silently return the wrong stem
    // (and collide ImGui button IDs derived from it).
    for (const auto& entry : kIconTable) {
        if (entry.icon == icon) {
            return entry.stem;
        }
    }
    return {};
}

} // namespace marrow::editor
