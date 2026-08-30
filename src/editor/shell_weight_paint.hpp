#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "shell_state.hpp"

namespace marrow::editor::shell {

struct WeightPaintSelectionContext {
    std::optional<std::size_t> target_slot_index;
    std::optional<PreviewAttachmentSelection> target_attachment;
    std::optional<std::size_t> influence_bone_index;
};

struct WeightPaintSample {
    ImVec2 screen_position{};
    float pressure{1.0f};
};

const char* weight_paint_mode_name(WeightPaintMode mode);
ImVec4 mesh_weight_heatmap_color(double weight, float alpha = 0.55f);
double weight_for_bone(
    const marrow::runtime::MeshGeometry::VertexWeights& vertex_weights,
    std::size_t bone_index);
WeightPaintSelectionContext resolve_weight_paint_selection_context(
    const ShellState& state);
std::optional<MeshWeightPaintTarget> current_mesh_weight_paint_target(
    const ShellState& state);
std::optional<MeshWeightOverlay> build_mesh_weight_overlay(
    const ShellState& state,
    const ViewportLayout& layout);
void reset_weight_paint_stroke(ShellState* state);
void begin_weight_paint_stroke(
    ShellState* state,
    const MeshWeightPaintTarget& target);
bool finish_weight_paint_stroke(ShellState* state);
bool apply_weight_paint_sample(
    ShellState* state,
    const MeshWeightOverlay& overlay,
    const WeightPaintSample& sample);
bool apply_weight_paint_sample(
    ShellState* state,
    const MeshWeightOverlay& overlay,
    const ImVec2& screen_position);

/**
 * @brief Vertex indices a scoped weight command addresses, or empty for all.
 *
 * A multi-vertex FFD selection that resolves against the current weight target
 * narrows the scope to exactly those vertices. Anything else -- no selection, a
 * selection on a different slot or attachment -- means every vertex, which is
 * the shipped `normalize_weights` behaviour.
 */
std::vector<std::size_t> weight_command_scope(const ShellState& state);

/**
 * @brief Canonicalizes the scoped vertices in one transaction and one history
 *        entry. A scope in which nothing changes neither dirties nor records.
 */
bool normalize_weights_command(ShellState* state);

/**
 * @brief Re-expresses the scoped vertices' bind offsets in each bone's setup
 *        frame, in one transaction and one history entry.
 */
bool rebind_weights_command(ShellState* state);

/**
 * @brief Writes one vertex's influence list through the canonical primitive.
 *
 * The numeric influence table's only mutation path. `influences` carries the
 * pre-canonical values the user typed; the weight a field redisplays afterwards
 * is the normalized one.
 */
bool set_active_vertex_weights_command(
    ShellState* state,
    std::size_t vertex_index,
    const std::vector<marrow::editor::MeshWeightInfluenceEdit>& influences);

} // namespace marrow::editor::shell
