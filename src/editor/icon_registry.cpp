#include "icon_registry.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <glad/glad.h>
#endif

#include "image_io.hpp"

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

std::uint32_t upload_rgba_texture(
    const std::vector<std::uint8_t>& rgba8,
    int width,
    int height) {
    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba8.data());
    glBindTexture(GL_TEXTURE_2D, 0);
    return static_cast<std::uint32_t>(texture);
}

} // namespace

IconRegistry::~IconRegistry() {
    unload_all();
}

int IconRegistry::load_all(const std::filesystem::path& root) {
    unload_all();

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
        slot.texture_id = upload_rgba_texture(rgba8, width, height);
        slot.width = width;
        slot.height = height;
        ++loaded;
    }
    all_loaded_ = loaded == static_cast<int>(Icon::Count);
    return loaded;
}

void IconRegistry::unload_all() {
    for (Slot& slot : slots_) {
        if (slot.texture_id != 0) {
            GLuint texture = slot.texture_id;
            glDeleteTextures(1, &texture);
            slot.texture_id = 0;
            slot.width = 0;
            slot.height = 0;
        }
    }
    all_loaded_ = false;
}

ImTextureID IconRegistry::get(Icon icon) const {
    const std::size_t index = static_cast<std::size_t>(icon);
    if (index >= slots_.size()) {
        return 0;
    }
    return static_cast<ImTextureID>(
        static_cast<std::uintptr_t>(slots_[index].texture_id));
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
    const std::size_t index = static_cast<std::size_t>(icon);
    if (index >= kIconTable.size()) {
        return {};
    }
    return kIconTable[index].stem;
}

} // namespace marrow::editor
