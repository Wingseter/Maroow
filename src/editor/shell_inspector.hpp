#pragma once

#include <vector>

#include "shell_state.hpp"

namespace marrow::editor::shell {

std::vector<MeshWeightVertexRow> build_mesh_weight_rows(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AttachmentData& attachment);
void draw_inspector_window(ShellState* state);

} // namespace marrow::editor::shell
