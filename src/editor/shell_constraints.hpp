#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marrow::editor::shell {

struct ShellState;
enum class ConstraintEditKind;

template <typename ConstraintType>
const ConstraintType* find_named_constraint(
    const std::vector<ConstraintType>& constraints,
    std::string_view name) {
    const auto iterator = std::find_if(
        constraints.begin(),
        constraints.end(),
        [&](const ConstraintType& constraint) {
            return constraint.name == name;
        });
    return iterator == constraints.end() ? nullptr : &(*iterator);
}

const char* constraint_kind_label(ConstraintEditKind kind);
void validate_selected_constraint(ShellState* state);
void select_constraint(
    ShellState* state,
    ConstraintEditKind kind,
    std::string_view name,
    std::string_view source,
    bool update_status_message);
std::string unique_constraint_name(
    const ShellState& state,
    ConstraintEditKind kind,
    std::string_view prefix);
std::optional<std::size_t> ensure_ik_constraint_edit_index(
    ShellState* state,
    std::string_view name);
std::optional<std::size_t> ensure_path_constraint_edit_index(
    ShellState* state,
    std::string_view name);
std::optional<std::size_t> ensure_transform_constraint_edit_index(
    ShellState* state,
    std::string_view name);
std::optional<std::size_t> ensure_physics_constraint_edit_index(
    ShellState* state,
    std::string_view name);
void draw_constraints_window(ShellState* state);

} // namespace marrow::editor::shell
