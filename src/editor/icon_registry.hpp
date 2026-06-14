#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

#include "imgui.h"

namespace marrow::editor {

enum class Icon : std::size_t {
    // Playback (P1)
    Play,
    Pause,
    Stop,
    Rewind,
    PrevKey,
    NextKey,
    AddKey,
    RemoveKey,
    Loop,

    // Overlays (P1)
    OnionSkin,
    MeshWire,
    Bbox,
    BoneToggle,
    PerfHud,
    Eye,

    // Weight paint (P1)
    WeightBrush,
    WeightErase,
    WeightSmooth,

    // Zoom (P1)
    ZoomFit,
    ZoomOne,

    // File ops (P1)
    Save,
    Export,
    Reload,
    Undo,
    Redo,
    MoveUp,
    MoveDown,

    // Tree nodes (P2)
    NodeBone,
    NodeSlot,
    NodeSkin,
    NodeAnim,

    // Attachments (P2)
    AttRegion,
    AttMesh,
    AttLinked,
    AttPoint,
    AttBbox,
    AttClip,
    AttPath,

    // Constraints (P2)
    ConstraintIk,
    ConstraintPath,
    ConstraintXform,
    ConstraintPhysics,

    // Properties (P3)
    PropRotate,
    PropTranslate,
    PropScale,
    PropShear,
    PropColor,
    PropOrder,
    PropEvent,

    // Status (P3)
    StatusWarn,
    StatusError,

    Count,
};

class IconRegistry {
public:
    IconRegistry() = default;
    ~IconRegistry();

    IconRegistry(const IconRegistry&) = delete;
    IconRegistry& operator=(const IconRegistry&) = delete;

    // Loads all 51 PNGs from `root` (expected to contain *.png). Returns
    // number of icons successfully loaded. On failure, subsequent calls to
    // `get_texture()` return 0 for missing icons (no crash).
    int load_all(const std::filesystem::path& root);

    void unload_all();

    ImTextureID get(Icon icon) const;
    ImVec2 size(Icon icon) const;

    // Returns false if any icon failed to load (for diagnostics).
    bool all_loaded() const;

private:
    struct Slot {
        std::uint32_t texture_id{0};
        int width{0};
        int height{0};
    };

    std::array<Slot, static_cast<std::size_t>(Icon::Count)> slots_{};
    bool all_loaded_{false};
};

// Returns the default filename stem for `icon` (matches PNG files in
// assets/icons/export/white_48/*.png).
std::string_view icon_filename_stem(Icon icon);

} // namespace marrow::editor
