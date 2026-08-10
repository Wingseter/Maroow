#pragma once

#include <cstddef>
#include <optional>

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

} // namespace marrow::editor::shell
