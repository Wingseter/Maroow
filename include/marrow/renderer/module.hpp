#pragma once

#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace marrow::renderer {

struct SampleAppWindow {
    std::string title{"Marrow Renderer Sample"};
    int width{1280};
    int height{720};
};

struct RenderPoint {
    double x{0.0};
    double y{0.0};
};

struct RegionAttachmentVertex {
    RenderPoint position;
    RenderPoint uv;
};

struct RegionAttachmentDrawCommand {
    std::string slot_name;
    std::string attachment_name;
    std::string atlas_region_name;
    std::size_t slot_index{0};
    std::size_t bone_index{0};
    std::array<RegionAttachmentVertex, 4> vertices{};
};

struct PreparedScene {
    std::string atlas_name;
    std::string atlas_image;
    std::string skeleton_name;
    std::vector<RegionAttachmentDrawCommand> region_attachments;
};

struct PreparedSceneResult {
    std::optional<PreparedScene> scene;
    std::string error_message;

    explicit operator bool() const {
        return scene.has_value();
    }
};

PreparedSceneResult prepare_setup_pose_scene(
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas);

class DemoShell {
public:
    DemoShell(SampleAppWindow window, PreparedScene scene);

    std::string launch_report() const;

private:
    SampleAppWindow window_;
    PreparedScene scene_;
};

std::string_view component_name();
std::string_view validation_target_name();

} // namespace marrow::renderer
