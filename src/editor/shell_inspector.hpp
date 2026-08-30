#pragma once

#include <optional>
#include <string>
#include <vector>

#include "shell_state.hpp"

namespace marrow::editor::shell {

std::vector<MeshWeightVertexRow> build_mesh_weight_rows(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AttachmentData& attachment);
bool inspector_bone_pose_editable(const ShellState& state) noexcept;

/**
 * @brief The one vertex the numeric influence table may edit, if any.
 *
 * Editing is offered for a single active vertex only: a numeric field that
 * writes one typed value into N vertices has no single correct meaning. The
 * vertex qualifies when the FFD selection holds exactly one index and its scope
 * resolves to the same slot and attachment as the current weight target.
 * `reason_out` receives a one-line explanation when it does not.
 */
std::optional<std::size_t> inspector_active_weight_vertex(
    const ShellState& state,
    std::string* reason_out);
void finalize_orphaned_inspector_transform_gesture(ShellState* state);
void draw_inspector_window(ShellState* state);

} // namespace marrow::editor::shell
