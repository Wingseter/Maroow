#pragma once

#include <vector>

#include "shell_state.hpp"

namespace marrow::editor::shell {

std::vector<MeshWeightVertexRow> build_mesh_weight_rows(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AttachmentData& attachment);
bool inspector_bone_pose_editable(const ShellState& state) noexcept;
void finalize_orphaned_inspector_transform_gesture(ShellState* state);
void draw_inspector_window(ShellState* state);

} // namespace marrow::editor::shell
