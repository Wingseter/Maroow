#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "shell_state.hpp"

namespace marrow::editor::shell::viewport_ffd {

inline constexpr float kVertexHandleHitRadius = 6.0f;

struct ViewportFfdOverlay {
    std::size_t slot_index{0U};
    std::optional<std::size_t> display_skin_index;
    std::string display_attachment_name;
    std::string deform_attachment_name;
    std::vector<ViewportWorldPoint> vertex_world_positions;
};

std::optional<ViewportFfdOverlay> build_overlay(const ShellState& state);
bool selection_matches_overlay(
    const ViewportFfdSelection& selection,
    const ViewportFfdOverlay& overlay);
void reconcile_selection(ShellState* state);
void clear_selection(ShellState* state);
bool select_vertex(
    ShellState* state,
    std::size_t vertex_index,
    bool toggle);
std::optional<std::size_t> hit_test_vertex(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position);
bool begin_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    std::size_t vertex_index,
    const ImVec2& pointer);
bool update_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer,
    ViewportSnapModifiers modifiers = {});
void finish_gesture(ShellState* state, bool commit);
bool begin_box_selection(
    ShellState* state,
    const ImVec2& pointer,
    bool additive);
bool update_box_selection(
    ShellState* state,
    const ImVec2& pointer);
bool finish_box_selection(
    ShellState* state,
    const ViewportLayout& layout,
    bool commit);
std::vector<std::size_t> box_preview_vertices(
    const ShellState& state,
    const ViewportLayout& layout);

} // namespace marrow::editor::shell::viewport_ffd
