#include "shell_timeline_graph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <utility>

#include "imgui.h"

#include "shell_preferences.hpp"
#include "shell_selection.hpp"
#include "timeline_controller.hpp"

namespace marrow::editor::shell {
namespace {

const timeline_graph_model::Projection kEmptyGraphProjection{};

constexpr ImU32 kGraphBackground = IM_COL32(0x17, 0x1a, 0x21, 0xff);
constexpr ImU32 kGraphGrid = IM_COL32(0x46, 0x4b, 0x57, 0x60);
constexpr ImU32 kGraphAxisText = IM_COL32(0xb7, 0xbd, 0xc9, 0xff);
constexpr ImU32 kGraphPlayhead = IM_COL32(0xff, 0x54, 0x50, 0xff);
constexpr ImU32 kGraphSelection = IM_COL32(0xff, 0xc1, 0x5c, 0xff);
constexpr ImU32 kGraphActiveCenter = IM_COL32(0xe6, 0xea, 0xf2, 0xff);
// Squares, not circles, so a handle never reads as a key point; light blue so
// it stays distinguishable from the gold selection ring at any component hue.
constexpr ImU32 kGraphHandleTangent = IM_COL32(0x9a, 0xd8, 0xff, 0x80);
constexpr ImU32 kGraphHandleFill = IM_COL32(0x9a, 0xd8, 0xff, 0xff);
// MAR-171: an automatic key's handles read as "derived, not authored" —
// hollow rather than filled, and amber rather than light blue, so they stay
// distinguishable from the gold selection ring by shape as well as hue.
constexpr ImU32 kAutoHandleTangent = IM_COL32(0xf0, 0xc0, 0x60, 0x80);
constexpr ImU32 kAutoHandleStroke = IM_COL32(0xf0, 0xc0, 0x60, 0xff);
constexpr float kGraphHandleHalfExtent = 4.0f;
constexpr double kGraphHandleHitRadius = 7.0;

ImU32 component_color(timeline_graph_model::Component component) {
    using Component = timeline_graph_model::Component;
    switch (component) {
    case Component::Angle: return IM_COL32(0xff, 0xc1, 0x5c, 0xff);
    case Component::X: return IM_COL32(0xff, 0x6b, 0x6b, 0xff);
    case Component::Y: return IM_COL32(0x63, 0xd4, 0x71, 0xff);
    case Component::Red: return IM_COL32(0xff, 0x5c, 0x5c, 0xff);
    case Component::Green: return IM_COL32(0x58, 0xd6, 0x8d, 0xff);
    case Component::Blue: return IM_COL32(0x5b, 0x8c, 0xff, 0xff);
    case Component::Alpha: return IM_COL32(0xe6, 0xea, 0xf2, 0xff);
    }
    return kGraphActiveCenter;
}

const char* component_label(timeline_graph_model::Component component) {
    using Component = timeline_graph_model::Component;
    switch (component) {
    case Component::Angle: return "Angle";
    case Component::X: return "X";
    case Component::Y: return "Y";
    case Component::Red: return "R";
    case Component::Green: return "G";
    case Component::Blue: return "B";
    case Component::Alpha: return "A";
    }
    return "?";
}

bool plot_contains(timeline_graph_model::PlotRect rect, double x, double y) {
    return x >= rect.min_x && x <= rect.max_x &&
        y >= rect.min_y && y <= rect.max_y;
}

bool key_is_selected(const ShellState& state, const TimelineKeyRef& key) {
    return std::find(
               state.timeline_editor.selected_keys.begin(),
               state.timeline_editor.selected_keys.end(),
               key) != state.timeline_editor.selected_keys.end();
}

void promote_displayed_fallback_focus(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    const TimelineTrackRow& displayed_track) {
    if (state == nullptr) return;
    if (state->selected_timeline_track_id.has_value() &&
        find_timeline_track(tracks, *state->selected_timeline_track_id) != nullptr) {
        return;
    }
    state->selected_timeline_track_id = displayed_track.id;
}

int tick_decimal_places(double step) {
    if (!std::isfinite(step) || step <= 0.0) return 0;
    return std::clamp(
        static_cast<int>(std::ceil(-std::log10(step))) + 1,
        0,
        6);
}

std::string fixed_label(double value, int decimals) {
    char buffer[64]{};
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return buffer;
}

void draw_time_ticks(
    ImDrawList* draw_list,
    timeline_graph_model::PlotRect rect,
    const timeline_graph_model::View& view,
    double frames_per_second) {
    const auto step = timeline_graph_model::nice_tick_interval(
        view.pixels_per_second, 72.0);
    if (!step.has_value()) return;
    const double visible_start = view.view_start_seconds;
    const double visible_end = visible_start +
        (rect.max_x - rect.min_x) / view.pixels_per_second;
    const double first = std::ceil(visible_start / *step) * *step;
    int decimals = tick_decimal_places(*step);
    if (frames_per_second >= 1.0) decimals = std::max(decimals, 3);
    for (std::size_t tick = 0U; tick < 2048U; ++tick) {
        const double time = first + static_cast<double>(tick) * *step;
        if (!std::isfinite(time) || time > visible_end + *step * 1e-6) break;
        const double x = rect.min_x +
            (time - view.view_start_seconds) * view.pixels_per_second;
        if (!plot_contains(rect, x, rect.min_y)) continue;
        draw_list->AddLine(
            ImVec2(static_cast<float>(x), static_cast<float>(rect.min_y)),
            ImVec2(static_cast<float>(x), static_cast<float>(rect.max_y)),
            kGraphGrid,
            1.0f);
        std::string label = fixed_label(time, decimals) + "s";
        if (frames_per_second >= 1.0 && std::isfinite(frames_per_second)) {
            label += "  f" + fixed_label(time * frames_per_second, 0);
        }
        draw_list->AddText(
            ImVec2(static_cast<float>(x + 3.0), static_cast<float>(rect.max_y + 3.0)),
            kGraphAxisText,
            label.c_str());
    }
}

bool draw_value_label(
    ImDrawList* draw_list,
    timeline_graph_model::PlotRect rect,
    double y,
    double value,
    int decimals) {
    if (!plot_contains(rect, rect.min_x, y)) return false;
    draw_list->AddLine(
        ImVec2(static_cast<float>(rect.min_x), static_cast<float>(y)),
        ImVec2(static_cast<float>(rect.max_x), static_cast<float>(y)),
        kGraphGrid,
        1.0f);
    const std::string label = fixed_label(value, decimals);
    const ImVec2 label_size = ImGui::CalcTextSize(label.c_str());
    draw_list->AddText(
        ImVec2(
            static_cast<float>(rect.min_x - 6.0) - label_size.x,
            static_cast<float>(y) - label_size.y * 0.5f),
        kGraphAxisText,
        label.c_str());
    return true;
}

bool tick_values_coincide(double value, double special, double step) {
    const double scale = std::max({1.0, std::abs(value), std::abs(special), std::abs(step)});
    return std::abs(value - special) <=
        std::numeric_limits<double>::epsilon() * scale * 16.0;
}

struct ValueTickDrawStats {
    std::size_t zero_label_count{0U};
    std::size_t one_label_count{0U};
};

ValueTickDrawStats draw_value_ticks(
    ImDrawList* draw_list,
    timeline_graph_model::PlotRect rect,
    const timeline_graph_model::View& view,
    timeline_graph_model::TrackKind kind) {
    ValueTickDrawStats stats;
    const auto step = timeline_graph_model::nice_tick_interval(
        view.pixels_per_value, 48.0);
    if (!step.has_value()) return stats;
    const double top_value = timeline_graph_model::value_at_y(rect, view, rect.min_y);
    const double bottom_value = timeline_graph_model::value_at_y(rect, view, rect.max_y);
    const double first = std::ceil(bottom_value / *step) * *step;
    const int decimals = tick_decimal_places(*step);
    for (std::size_t tick = 0U; tick < 2048U; ++tick) {
        const double value = first + static_cast<double>(tick) * *step;
        if (!std::isfinite(value) || value > top_value + *step * 1e-6) break;
        if (kind == timeline_graph_model::TrackKind::SlotColor &&
            (tick_values_coincide(value, 0.0, *step) ||
             tick_values_coincide(value, 1.0, *step))) {
            continue;
        }
        if (draw_value_label(
                draw_list,
                rect,
                timeline_graph_model::y_at_value(rect, view, value),
                value,
                decimals)) {
            if (tick_values_coincide(value, 0.0, *step)) ++stats.zero_label_count;
            if (tick_values_coincide(value, 1.0, *step)) ++stats.one_label_count;
        }
    }
    if (kind == timeline_graph_model::TrackKind::SlotColor) {
        if (draw_value_label(
                draw_list,
                rect,
                timeline_graph_model::y_at_value(rect, view, 0.0),
                0.0,
                decimals)) {
            ++stats.zero_label_count;
        }
        if (draw_value_label(
                draw_list,
                rect,
                timeline_graph_model::y_at_value(rect, view, 1.0),
                1.0,
                decimals)) {
            ++stats.one_label_count;
        }
    }
    return stats;
}

const char* outgoing_kind_label(
    const timeline_graph_model::Track& track,
    const std::optional<TimelineKeyRef>& active_key) {
    if (!active_key.has_value()) return "No outgoing segment";
    const auto it = std::find_if(
        track.keys.begin(),
        track.keys.end(),
        [&](const timeline_graph_model::Key& key) {
            return key.identity == *active_key;
        });
    if (it == track.keys.end() || std::next(it) == track.keys.end()) {
        return "No outgoing segment";
    }
    // MAR-170: the readout is a pure function of the stored curve, recomputed
    // every frame, so it needs no invalidation and reads Custom Bezier the
    // moment a handle drag moves away from a preset.
    const auto preset = marrow::editor::curve_preset_of(it->outgoing_easing);
    if (!preset.has_value()) return "Custom Bezier";
    // Every table display_name is a string literal, so .data() is
    // null-terminated and outlives the call.
    return marrow::editor::curve_preset_definition(*preset).display_name.data();
}

bool fit_graph_view(
    ShellState* state,
    const timeline_graph_model::Track& track,
    timeline_graph_model::PlotRect rect) {
    const auto fitted = timeline_graph_model::fit_view(
        track,
        state->timeline_editor.graph_view.component_visible,
        rect,
        state->timeline_editor.frames_per_second);
    if (!fitted.has_value()) return false;
    auto& graph_view = state->timeline_editor.graph_view;
    graph_view.view = *fitted;
    graph_view.fitted_track_id = track.track_id;
    graph_view.fitted_animation_name = state->selected_animation_name;
    graph_view.needs_fit = false;
    return true;
}

bool graph_view_is_finite(const timeline_graph_model::View& view) {
    return std::isfinite(view.view_start_seconds) &&
        std::isfinite(view.pixels_per_second) && view.pixels_per_second > 0.0 &&
        std::isfinite(view.value_center) &&
        std::isfinite(view.pixels_per_value) && view.pixels_per_value > 0.0;
}

/**
 * @brief One-line readout of the live drag, or empty when none is live.
 *
 * The Time axis reports seconds and frames; the Value axis reports the active
 * component, using six decimals for Slot Color and three otherwise.
 */
std::string timeline_graph_drag_readout(
    const ShellState& state,
    timeline_graph_model::TrackKind kind) {
    if (!state.timeline_editor.graph_drag.has_value()) return {};
    const TimelineGraphPointDrag& drag = *state.timeline_editor.graph_drag;
    char buffer[192]{};
    if (drag.axis == timeline_graph_model::DragAxis::Time &&
        state.timeline_editor.retime_gesture.has_value()) {
        const double applied = state.timeline_editor.retime_gesture->applied_delta;
        const double current = drag.press_time_seconds + applied;
        std::snprintf(
            buffer,
            sizeof(buffer),
            "Time  %.3fs -> %.3fs  (delta %.3fs, %.2f f)",
            drag.press_time_seconds,
            current,
            applied,
            applied * state.timeline_editor.frames_per_second);
        return buffer;
    }
    if (drag.axis == timeline_graph_model::DragAxis::Value &&
        state.timeline_editor.graph_value_gesture.has_value()) {
        const double applied =
            state.timeline_editor.graph_value_gesture->applied_delta;
        const int decimals =
            kind == timeline_graph_model::TrackKind::SlotColor ? 6 : 3;
        std::snprintf(
            buffer,
            sizeof(buffer),
            "%s  %.*f -> %.*f  (delta %.*f)",
            component_label(drag.component),
            decimals,
            drag.press_value,
            decimals,
            drag.press_value + applied,
            decimals,
            applied);
        return buffer;
    }
    return {};
}

/**
 * @brief Resolves the one component the handles are drawn and grabbed for.
 *
 * One curve gets exactly one pair of handles: drawing a pair per visible
 * component would suggest each component owns its own curve, which is the
 * misconception the shared-easing contract exists to prevent. Both the
 * geometry call and the press branch read this, so the drawn handle and the
 * grabbed handle always agree.
 */
std::optional<std::size_t> resolve_handle_component_index(
    const timeline_graph_model::Track& track,
    const TimelineGraphViewState& graph_view) {
    if (graph_view.active_component.has_value()) {
        for (std::size_t index = 0U; index < track.components.size(); ++index) {
            if (track.components[index].component == *graph_view.active_component &&
                graph_view.component_visible[index]) {
                return index;
            }
        }
    }
    for (std::size_t index = 0U; index < track.components.size(); ++index) {
        if (graph_view.component_visible[index]) return index;
    }
    return std::nullopt;
}

const char* interpolation_kind_label(marrow::runtime::InterpolationKind kind) {
    switch (kind) {
    case marrow::runtime::InterpolationKind::Linear: return "Linear";
    case marrow::runtime::InterpolationKind::Stepped: return "Stepped";
    case marrow::runtime::InterpolationKind::CubicBezier: return "Bezier";
    }
    return "Linear";
}

/** @brief One-line readout of the live easing drag, or empty when none is. */
std::string timeline_graph_easing_readout(const ShellState& state) {
    if (!state.timeline_editor.graph_handle_gesture.has_value()) return {};
    const TimelineGraphHandleGesture& gesture =
        *state.timeline_editor.graph_handle_gesture;
    char buffer[224]{};
    std::snprintf(
        buffer,
        sizeof(buffer),
        "Easing  %s -> [%.3f, %.3f, %.3f, %.3f]%s%s",
        interpolation_kind_label(gesture.original_kind),
        gesture.applied_control_points[0],
        gesture.applied_control_points[1],
        gesture.applied_control_points[2],
        gesture.applied_control_points[3],
        gesture.clamped_x ? "  (X clamped)" : "",
        gesture.frame.flat_value_span ? "  (flat segment: 100 px = 1.0)" : "");
    return buffer;
}

std::array<bool, 4> available_components(
    const timeline_graph_model::Projection& projection) {
    std::array<bool, 4> visible{false, false, false, false};
    if (!projection.track.has_value()) return visible;
    const std::size_t count = std::min(visible.size(), projection.track->components.size());
    std::fill_n(visible.begin(), count, true);
    return visible;
}

} // namespace

void draw_timeline_curve_preset_row(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    TimelineGraphRenderStats* stats) {
    if (state == nullptr) return;
    const auto& presets = marrow::editor::kCurvePresets;
    // Short button labels; the tooltip carries the full name and the exact
    // quadruple, so the row stays narrow without hiding the constants.
    static constexpr std::array<const char*, 6> kShortLabels{
        "Linear", "Stepped", "Ease", "In", "Out", "In-Out"};

    const std::size_t compatible =
        compatible_curve_preset_key_count(*state, tracks);
    const bool gesture_live = authoring_gesture_active(*state);
    const bool enabled = compatible > 0U && !gesture_live;
    if (stats != nullptr) {
        stats->curve_preset_row_drawn = true;
        stats->curve_preset_row_enabled = enabled;
        stats->default_preset_index =
            static_cast<std::size_t>(state->preferences.default_curve);
        const auto active = active_outgoing_curve_preset(*state, tracks);
        stats->active_preset_index = active.has_value()
            ? static_cast<std::size_t>(*active)
            : presets.size();
    }

    ImGui::TextDisabled("Curve:");
    ImGui::BeginDisabled(!enabled);
    std::optional<marrow::editor::CurvePreset> requested;
    for (std::size_t index = 0U; index < presets.size(); ++index) {
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(index));
        const bool clicked = ImGui::SmallButton(kShortLabels[index]);
        if (index == 0U && stats != nullptr) {
            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            stats->first_preset_min_x = item_min.x;
            stats->first_preset_min_y = item_min.y;
            stats->first_preset_max_x = item_max.x;
            stats->first_preset_max_y = item_max.y;
        }
        // The tooltip is built per button, inside the loop, so every button
        // gets one. Reading the hover state after EndDisabled() would test only
        // the last button and would overwrite that button's own tooltip.
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            const auto& definition = presets[index];
            std::string tooltip(definition.display_name);
            if (definition.kind == marrow::runtime::InterpolationKind::CubicBezier) {
                char points[96]{};
                std::snprintf(
                    points,
                    sizeof(points),
                    "  [%g, %g, %g, %g]",
                    definition.control_points[0],
                    definition.control_points[1],
                    definition.control_points[2],
                    definition.control_points[3]);
                tooltip += points;
            }
            // A disabled row teaches its constraint; a vanished row does not.
            if (gesture_live) {
                tooltip += "\nFinish the active edit before applying a curve preset";
            } else if (compatible == 0U) {
                tooltip +=
                    "\nSelect one or more Transform, Deform, or Slot Color keys";
            }
            ImGui::SetTooltip("%s", tooltip.c_str());
        }
        if (clicked) requested = presets[index].preset;
        ImGui::PopID();
    }
    ImGui::EndDisabled();
    if (requested.has_value()) {
        apply_timeline_curve_preset(state, tracks, *requested);
    }

    // The remembered default. It is changed only here: applying a preset must
    // never have a persistent, cross-project side effect.
    ImGui::SameLine();
    ImGui::TextDisabled("Default:");
    ImGui::SameLine();
    ImGui::BeginDisabled(gesture_live);
    const auto& current_default =
        marrow::editor::curve_preset_definition(state->preferences.default_curve);
    const std::string current_default_label(current_default.display_name);
    ImGui::SetNextItemWidth(
        ImGui::CalcTextSize("Ease-In-Out").x + ImGui::GetFrameHeight() +
        ImGui::GetStyle().FramePadding.x * 4.0f);
    if (ImGui::BeginCombo("##curve_default", current_default_label.c_str())) {
        for (const auto& definition : presets) {
            const std::string label(definition.display_name);
            const bool selected = definition.preset == state->preferences.default_curve;
            if (ImGui::Selectable(label.c_str(), selected)) {
                set_shell_default_curve(state, definition.preset);
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            "Seeds newly added Transform, Deform, and Slot Color keys. Stored per "
            "user in editor-settings.json; it never changes existing keys and never "
            "modifies the project.");
    }
}


namespace {

/** @brief The display name of one driver component. */
const char* curve_driver_display_label(
    marrow::editor::TimelineScalarComponent driver) {
    switch (driver) {
    case marrow::editor::TimelineScalarComponent::Angle: return "Angle";
    case marrow::editor::TimelineScalarComponent::X: return "X";
    case marrow::editor::TimelineScalarComponent::Y: return "Y";
    case marrow::editor::TimelineScalarComponent::Red: return "Red";
    case marrow::editor::TimelineScalarComponent::Green: return "Green";
    case marrow::editor::TimelineScalarComponent::Blue: return "Blue";
    case marrow::editor::TimelineScalarComponent::Alpha: return "Alpha";
    }
    return "Angle";
}

} // namespace

void draw_timeline_curve_mode_row(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    TimelineGraphRenderStats* stats) {
    if (state == nullptr) return;
    using marrow::editor::TimelineCurveMode;
    using marrow::editor::TimelineScalarComponent;

    const std::size_t compatible = compatible_curve_mode_key_count(*state, tracks);
    const bool gesture_live = authoring_gesture_active(*state);
    const bool enabled = compatible > 0U && !gesture_live;

    // The drivers every compatible selected family owns. A Rotate key and a
    // Slot Color key together own no component in common, so the combo is
    // disabled rather than offering a driver one of them would reject.
    const auto& selection = state->timeline_editor.selected_keys;
    bool has_rotate = false;
    bool has_vector = false;
    bool has_color = false;
    for (const TimelineKeyRef& key : selection) {
        for (const TimelineTrackRow& row : tracks) {
            if (!timeline_track_is_editable(row)) continue;
            if (!timeline_key_index(row, key).has_value()) continue;
            if (row.transform_channel.has_value()) {
                if (*row.transform_channel ==
                    marrow::editor::TransformTimelineChannel::Rotate) {
                    has_rotate = true;
                } else {
                    has_vector = true;
                }
            } else if (row.id.find(":Color") != std::string::npos) {
                has_color = true;
            }
            break;
        }
    }
    std::vector<TimelineScalarComponent> drivers;
    const int family_count =
        (has_rotate ? 1 : 0) + (has_vector ? 1 : 0) + (has_color ? 1 : 0);
    if (family_count == 1) {
        if (has_rotate) {
            drivers = {TimelineScalarComponent::Angle};
        } else if (has_vector) {
            drivers = {TimelineScalarComponent::X, TimelineScalarComponent::Y};
        } else {
            drivers = {
                TimelineScalarComponent::Red, TimelineScalarComponent::Green,
                TimelineScalarComponent::Blue, TimelineScalarComponent::Alpha};
        }
    }
    const bool driver_enabled = enabled && !drivers.empty();
    if (std::find(drivers.begin(), drivers.end(), state->timeline_editor.curve_driver) ==
        drivers.end()) {
        state->timeline_editor.curve_driver =
            drivers.empty() ? TimelineScalarComponent::Angle : drivers.front();
    }

    if (stats != nullptr) {
        stats->curve_mode_row_drawn = true;
        stats->curve_mode_row_enabled = enabled;
        const auto mode = active_outgoing_curve_mode(*state, tracks);
        const auto driver = active_outgoing_curve_driver(*state, tracks);
        stats->active_key_auto = mode.has_value() && *mode == TimelineCurveMode::Auto;
        stats->active_driver_index = stats->active_key_auto && driver.has_value()
            ? static_cast<std::size_t>(*driver)
            : kCurveDriverCount;
    }

    ImGui::TextDisabled("Curve mode:");
    ImGui::BeginDisabled(!enabled);
    std::optional<TimelineCurveMode> requested;
    ImGui::SameLine();
    const bool manual_clicked = ImGui::SmallButton("Manual");
    if (stats != nullptr) {
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        stats->first_curve_mode_min_x = item_min.x;
        stats->first_curve_mode_min_y = item_min.y;
        stats->first_curve_mode_max_x = item_max.x;
        stats->first_curve_mode_max_y = item_max.y;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        std::string tooltip(
            "Manual  the stored easing is exactly what you put there");
        if (gesture_live) {
            tooltip += "\nFinish the active edit before changing the curve mode";
        } else if (compatible == 0U) {
            tooltip += "\nSelect one or more Transform or Slot Color keys";
        }
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
    if (manual_clicked) requested = TimelineCurveMode::Manual;
    ImGui::SameLine();
    const bool auto_clicked = ImGui::SmallButton("Auto");
    if (stats != nullptr) {
        const ImVec2 item_min = ImGui::GetItemRectMin();
        const ImVec2 item_max = ImGui::GetItemRectMax();
        stats->auto_curve_mode_min_x = item_min.x;
        stats->auto_curve_mode_min_y = item_min.y;
        stats->auto_curve_mode_max_x = item_max.x;
        stats->auto_curve_mode_max_y = item_max.y;
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        std::string tooltip(
            "Auto  the easing is recomputed from the driver's neighbouring keys "
            "whenever they move, and never overshoots. Dragging a handle switches "
            "the segment back to manual.");
        if (gesture_live) {
            tooltip += "\nFinish the active edit before changing the curve mode";
        } else if (compatible == 0U) {
            tooltip += "\nSelect one or more Transform or Slot Color keys";
        }
        ImGui::SetTooltip("%s", tooltip.c_str());
    }
    if (auto_clicked) requested = TimelineCurveMode::Auto;
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::TextDisabled("Driver:");
    ImGui::SameLine();
    ImGui::BeginDisabled(!driver_enabled);
    ImGui::SetNextItemWidth(
        ImGui::CalcTextSize("Alpha").x + ImGui::GetFrameHeight() +
        ImGui::GetStyle().FramePadding.x * 4.0f);
    if (ImGui::BeginCombo(
            "##curve_driver",
            curve_driver_display_label(state->timeline_editor.curve_driver))) {
        for (const TimelineScalarComponent driver : drivers) {
            const bool selected = driver == state->timeline_editor.curve_driver;
            if (ImGui::Selectable(curve_driver_display_label(driver), selected)) {
                state->timeline_editor.curve_driver = driver;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !driver_enabled) {
        ImGui::SetTooltip(
            "Select keys of one timeline family to choose a driver");
    }

    if (requested.has_value()) {
        apply_timeline_curve_mode(
            state,
            tracks,
            *requested,
            *requested == TimelineCurveMode::Auto && !drivers.empty()
                ? std::optional<TimelineScalarComponent>(
                      state->timeline_editor.curve_driver)
                : std::nullopt);
    }
}

const TimelineTrackRow* resolve_timeline_graph_track(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state.selected_timeline_track_id.has_value()) {
        if (const TimelineTrackRow* focused = find_timeline_track(
                tracks, *state.selected_timeline_track_id)) {
            return focused;
        }
    }

    const ResolvedSelection resolved = resolve_shell_selection(state);
    const auto matches_active_context = [&](const TimelineTrackRow& track) {
        return (track.bone_index.has_value() &&
                resolved.active_bone_index == track.bone_index) ||
            (track.slot_index.has_value() &&
             resolved.active_slot_index == track.slot_index);
    };
    const auto it = std::find_if(
        tracks.begin(),
        tracks.end(),
        [&](const TimelineTrackRow& track) {
            return timeline_graph_model::track_is_supported(track) &&
                matches_active_context(track);
        });
    return it == tracks.end() ? nullptr : &*it;
}

const timeline_graph_model::Projection& cached_timeline_graph_projection(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (state == nullptr) return kEmptyGraphProjection;

    auto& cache = state->timeline_editor.graph_cache;
    auto& graph_view = state->timeline_editor.graph_view;
    const std::uint64_t runtime_revision = state->session.runtime_revision();
    const marrow::runtime::SkeletonData* skeleton = state->session.runtime_data();
    const bool cache_matches = cache.valid &&
        cache.runtime_revision == runtime_revision &&
        cache.skeleton_identity == skeleton &&
        cache.animation_name == state->selected_animation_name &&
        cache.track_id == track.id;

    if (!cache_matches) {
        const bool context_changed = !cache.valid ||
            cache.animation_name != state->selected_animation_name ||
            cache.track_id != track.id;
        TimelineGraphProjectionCache rebuilt;
        rebuilt.runtime_revision = runtime_revision;
        rebuilt.skeleton_identity = skeleton;
        rebuilt.animation_name = state->selected_animation_name;
        rebuilt.track_id = track.id;
        rebuilt.generation = cache.generation + 1U;
        rebuilt.valid = true;
        if (const auto* animation = selected_animation(*state)) {
            rebuilt.projection = timeline_graph_model::project_track(*animation, track);
        } else {
            rebuilt.projection.status = timeline_graph_model::ProjectionStatus::MissingSource;
        }
        cache = std::move(rebuilt);

        if (context_changed) {
            graph_view.component_visible = available_components(cache.projection);
            graph_view.active_component.reset();
            graph_view.needs_fit = true;
        }
    }

    if (!graph_view_is_finite(graph_view.view)) {
        graph_view.needs_fit = true;
    }
    return cache.projection;
}

bool activate_timeline_graph_point(
    ShellState* state,
    const TimelineTrackRow& track,
    const timeline_graph_model::PointHit& point,
    bool additive,
    std::string_view source) {
    if (state == nullptr) return false;
    const auto key_index = timeline_key_index(track, point.key);
    if (!key_index.has_value() ||
        !activate_timeline_key(
            state, track, *key_index, additive, source, true)) {
        return false;
    }
    state->timeline_editor.graph_view.active_component = point.component;
    return true;
}

bool begin_timeline_graph_point_drag(
    ShellState* state,
    const TimelineTrackRow& track,
    const timeline_graph_model::PointHit& point,
    std::uint32_t item_id,
    timeline_graph_model::PlotRect plot,
    const timeline_graph_model::View& view,
    double pointer_x,
    double pointer_y) {
    if (state == nullptr || authoring_gesture_active(*state) ||
        !timeline_track_is_editable(track) ||
        !timeline_graph_component_is_editable(track, point.component) ||
        !key_is_selected(*state, point.key) ||
        !std::isfinite(pointer_x) || !std::isfinite(pointer_y) ||
        !graph_view_is_finite(view) ||
        !std::isfinite(plot.min_x) || !std::isfinite(plot.min_y) ||
        !std::isfinite(plot.max_x) || !std::isfinite(plot.max_y)) {
        return false;
    }
    if (!timeline_key_index(track, point.key).has_value()) return false;
    const auto& projection = cached_timeline_graph_projection(state, track);
    if (projection.status != timeline_graph_model::ProjectionStatus::Ready ||
        !projection.track.has_value()) {
        return false;
    }
    const auto projected = std::find_if(
        projection.track->keys.begin(),
        projection.track->keys.end(),
        [&](const timeline_graph_model::Key& candidate) {
            return candidate.identity == point.key;
        });
    if (projected == projection.track->keys.end() ||
        point.component_index >= projected->value_count) {
        return false;
    }

    TimelineGraphPointDrag drag;
    drag.item_id = item_id;
    drag.axis = timeline_graph_model::DragAxis::Undecided;
    drag.track_id = track.id;
    drag.component = point.component;
    drag.press_pointer_x = pointer_x;
    drag.press_pointer_y = pointer_y;
    drag.press_time_seconds = projected->time_seconds;
    drag.press_value = projected->values[point.component_index];
    drag.frozen_view = view;
    state->timeline_editor.graph_drag.emplace(std::move(drag));
    return true;
}

bool begin_timeline_graph_handle_drag(
    ShellState* state,
    const TimelineTrackRow& track,
    const timeline_graph_model::HandleGeometry& handles,
    timeline_graph_model::HandleIndex handle,
    std::uint32_t item_id,
    timeline_graph_model::PlotRect plot,
    const timeline_graph_model::View& view,
    double pointer_x,
    double pointer_y) {
    if (state == nullptr || authoring_gesture_active(*state) ||
        state->timeline_editor.graph_drag.has_value() ||
        !timeline_track_is_editable(track) ||
        !timeline_graph_component_is_editable(track, handles.component) ||
        !std::isfinite(pointer_x) || !std::isfinite(pointer_y) ||
        !graph_view_is_finite(view) ||
        !std::isfinite(plot.min_x) || !std::isfinite(plot.min_y) ||
        !std::isfinite(plot.max_x) || !std::isfinite(plot.max_y)) {
        return false;
    }
    // Handles belong to the active key's outgoing segment only.
    if (!state->timeline_editor.active_key.has_value() ||
        !(*state->timeline_editor.active_key == handles.key)) {
        return false;
    }
    if (!timeline_key_index(track, handles.key).has_value()) return false;
    const auto& projection = cached_timeline_graph_projection(state, track);
    if (projection.status != timeline_graph_model::ProjectionStatus::Ready ||
        !projection.track.has_value()) {
        return false;
    }
    if (!std::isfinite(handles.frame.time_span) ||
        !(handles.frame.time_span > timeline_graph_model::kMinimumSegmentSeconds) ||
        !std::isfinite(handles.frame.value_span) ||
        handles.frame.value_span == 0.0 ||
        !std::isfinite(handles.frame.start_time_seconds) ||
        !std::isfinite(handles.frame.start_value)) {
        return false;
    }

    marrow::runtime::InterpolationKind segment_kind =
        marrow::runtime::InterpolationKind::Linear;
    switch (handles.kind) {
    case timeline_graph_model::SegmentKind::Linear:
        segment_kind = marrow::runtime::InterpolationKind::Linear;
        break;
    case timeline_graph_model::SegmentKind::Stepped:
        segment_kind = marrow::runtime::InterpolationKind::Stepped;
        break;
    case timeline_graph_model::SegmentKind::Cubic:
        segment_kind = marrow::runtime::InterpolationKind::CubicBezier;
        break;
    }

    TimelineGraphPointDrag drag;
    drag.item_id = item_id;
    drag.target = GraphDragTarget::Handle;
    drag.axis = timeline_graph_model::DragAxis::Undecided;
    drag.track_id = track.id;
    drag.component = handles.component;
    drag.press_pointer_x = pointer_x;
    drag.press_pointer_y = pointer_y;
    drag.press_time_seconds = handles.frame.start_time_seconds;
    drag.press_value = handles.frame.start_value;
    drag.frozen_view = view;
    drag.frozen_plot = plot;
    drag.pressed_key = handles.key;
    drag.handle = handle;
    drag.frame = handles.frame;
    drag.seed_control_points = handles.control_points;
    drag.segment_kind = segment_kind;
    state->timeline_editor.graph_drag.emplace(std::move(drag));
    return true;
}

void cancel_timeline_graph_point_drag(ShellState* state) {
    if (state == nullptr || !state->timeline_editor.graph_drag.has_value()) return;
    const GraphDragTarget target = state->timeline_editor.graph_drag->target;
    const timeline_graph_model::DragAxis axis = state->timeline_editor.graph_drag->axis;
    state->timeline_editor.graph_drag.reset();
    if (target == GraphDragTarget::Handle) {
        finish_timeline_graph_handle_gesture(state, false);
        return;
    }
    if (axis == timeline_graph_model::DragAxis::Time) {
        finish_timeline_retime_gesture(state, false);
    } else if (axis == timeline_graph_model::DragAxis::Value) {
        finish_timeline_graph_value_gesture(state, false);
    }
}

namespace {

/**
 * @brief Advances the Handle branch of the shared graph drag driver.
 *
 * A handle drag is free 2-D: `decide_drag_axis()` is deliberately not called,
 * because `cx` and `cy` are two parameters of one curve written by one
 * primitive, so MAR-168's axis lock has no rollback proof to protect here and
 * would make the curve un-authorable.
 */
bool update_timeline_graph_handle_drag(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    const TimelineTrackRow* row,
    double pointer_x,
    double pointer_y) {
    TimelineGraphPointDrag& drag = *state->timeline_editor.graph_drag;
    // The handle belongs to the active key; losing that identity mid-drag
    // would write the curve of a key the user never grabbed.
    if (!state->timeline_editor.active_key.has_value() ||
        !(*state->timeline_editor.active_key == drag.pressed_key)) {
        const bool had_gesture =
            state->timeline_editor.graph_handle_gesture.has_value();
        cancel_timeline_graph_point_drag(state);
        if (!had_gesture) {
            state->status_message = "The graph editing context changed during editing";
        }
        return false;
    }

    if (!state->timeline_editor.graph_handle_gesture.has_value()) {
        if (!std::isfinite(pointer_x) || !std::isfinite(pointer_y)) {
            state->timeline_editor.graph_drag.reset();
            state->status_message = "Graph easing drag pointer became unusable";
            return false;
        }
        if (std::max(
                std::abs(pointer_x - drag.press_pointer_x),
                std::abs(pointer_y - drag.press_pointer_y)) <
            timeline_graph_model::kDragDeadZonePixels) {
            return true;
        }
        if (!begin_timeline_graph_handle_gesture(
                state,
                drag.item_id,
                *row,
                drag.pressed_key,
                drag.frame,
                drag.seed_control_points,
                drag.segment_kind,
                tracks)) {
            state->timeline_editor.graph_drag.reset();
            state->status_message = "Could not start the graph easing drag";
            return false;
        }
    }

    const TimelineGraphPointDrag& live = *state->timeline_editor.graph_drag;
    const std::array<double, 4> applied =
        state->timeline_editor.graph_handle_gesture->applied_control_points;
    const auto control_points =
        timeline_graph_model::control_points_from_handle_pointer(
            live.frame,
            applied,
            live.handle,
            live.frozen_view,
            live.frozen_plot,
            pointer_x,
            pointer_y);
    if (!control_points.has_value()) {
        state->timeline_editor.graph_drag.reset();
        finish_timeline_graph_handle_gesture(state, false);
        state->status_message = "Graph easing drag pointer became unusable";
        return false;
    }
    state->timeline_editor.graph_handle_gesture->clamped_x =
        (*control_points)[live.handle == timeline_graph_model::HandleIndex::First
                              ? 0U
                              : 2U] == 0.0 ||
        (*control_points)[live.handle == timeline_graph_model::HandleIndex::First
                              ? 0U
                              : 2U] == 1.0;
    if (!apply_timeline_graph_handle_control_points(state, tracks, *control_points)) {
        // apply_* already cancelled its own gesture.
        state->timeline_editor.graph_drag.reset();
        return false;
    }
    return true;
}

} // namespace

bool update_timeline_graph_point_drag(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double pointer_x,
    double pointer_y,
    bool pointer_down,
    bool cancel_requested,
    bool bypass_frame_snap) {
    if (state == nullptr || !state->timeline_editor.graph_drag.has_value()) return false;
    if (cancel_requested) {
        cancel_timeline_graph_point_drag(state);
        return false;
    }
    const auto finish_live_axis = [&](bool commit) {
        const GraphDragTarget target = state->timeline_editor.graph_drag->target;
        const timeline_graph_model::DragAxis axis =
            state->timeline_editor.graph_drag->axis;
        state->timeline_editor.graph_drag.reset();
        if (target == GraphDragTarget::Handle) {
            finish_timeline_graph_handle_gesture(state, commit);
            return;
        }
        if (axis == timeline_graph_model::DragAxis::Time) {
            finish_timeline_retime_gesture(state, commit);
        } else if (axis == timeline_graph_model::DragAxis::Value) {
            finish_timeline_graph_value_gesture(state, commit);
        }
    };
    if (!pointer_down) {
        finish_live_axis(true);
        return false;
    }
    // The focused row must still exist, still be editable, and still project;
    // an out-of-band change cancels rather than writing to the wrong key.
    const TimelineTrackRow* row =
        find_timeline_track(tracks, state->timeline_editor.graph_drag->track_id);
    if (row == nullptr || !timeline_track_is_editable(*row) ||
        !timeline_graph_component_is_editable(
            *row, state->timeline_editor.graph_drag->component) ||
        cached_timeline_graph_projection(state, *row).status !=
            timeline_graph_model::ProjectionStatus::Ready) {
        const bool had_gesture =
            state->timeline_editor.graph_drag->axis !=
                timeline_graph_model::DragAxis::Undecided ||
            state->timeline_editor.graph_handle_gesture.has_value();
        cancel_timeline_graph_point_drag(state);
        if (!had_gesture) {
            state->status_message = "The graph editing context changed during editing";
        }
        return false;
    }

    if (state->timeline_editor.graph_drag->target == GraphDragTarget::Handle) {
        return update_timeline_graph_handle_drag(state, tracks, row, pointer_x, pointer_y);
    }

    TimelineGraphPointDrag& drag = *state->timeline_editor.graph_drag;
    if (drag.axis == timeline_graph_model::DragAxis::Undecided) {
        // decide_drag_axis runs only while the axis is Undecided: re-deciding
        // per frame would silently reintroduce diagonal editing.
        const auto axis = timeline_graph_model::decide_drag_axis(
            drag.press_pointer_x,
            drag.press_pointer_y,
            pointer_x,
            pointer_y,
            timeline_graph_model::kDragDeadZonePixels);
        if (axis == timeline_graph_model::DragAxis::Undecided) return true;
        const bool started = axis == timeline_graph_model::DragAxis::Time
            ? begin_timeline_retime_gesture(
                  state, drag.item_id, static_cast<float>(drag.press_pointer_x), tracks)
            : begin_timeline_graph_value_gesture(
                  state, drag.item_id, *row, drag.component, tracks);
        if (!started) {
            state->timeline_editor.graph_drag.reset();
            state->status_message = axis == timeline_graph_model::DragAxis::Time
                ? "Could not start the graph time drag"
                : "Could not start the graph value drag";
            return false;
        }
        state->timeline_editor.graph_drag->axis = axis;
    }

    const TimelineGraphPointDrag& live = *state->timeline_editor.graph_drag;
    if (live.axis == timeline_graph_model::DragAxis::Time) {
        const auto delta = timeline_graph_model::drag_time_delta(
            live.frozen_view, live.press_pointer_x, pointer_x);
        if (!delta.has_value()) {
            state->timeline_editor.graph_drag.reset();
            finish_timeline_retime_gesture(state, false);
            state->status_message = "Graph time drag pointer became unusable";
            return false;
        }
        const bool snap =
            state->timeline_editor.snap_to_frames && !bypass_frame_snap;
        if (!apply_timeline_retime_delta(state, tracks, *delta, snap)) {
            // apply_timeline_retime_delta self-cancels its own gesture.
            state->timeline_editor.graph_drag.reset();
            return false;
        }
        return true;
    }

    const auto delta = timeline_graph_model::drag_value_delta(
        live.frozen_view, live.press_pointer_y, pointer_y);
    if (!delta.has_value()) {
        state->timeline_editor.graph_drag.reset();
        finish_timeline_graph_value_gesture(state, false);
        state->status_message = "Graph value drag pointer became unusable";
        return false;
    }
    if (!apply_timeline_graph_value_delta(state, tracks, *delta)) {
        // apply_timeline_graph_value_delta self-cancels its own gesture.
        state->timeline_editor.graph_drag.reset();
        return false;
    }
    return true;
}

void poll_timeline_graph_point_drag(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || !state->timeline_editor.graph_drag.has_value()) return;
    const ImGuiIO& io = ImGui::GetIO();
    (void)update_timeline_graph_point_drag(
        state,
        tracks,
        static_cast<double>(io.MousePos.x),
        static_cast<double>(io.MousePos.y),
        ImGui::IsMouseDown(ImGuiMouseButton_Left),
        ImGui::IsKeyPressed(ImGuiKey_Escape, false),
        io.KeyAlt);
}

TimelineGraphRenderStats draw_timeline_graph_body(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    TimelineGraphRenderStats stats;
    if (state == nullptr) return stats;

    const TimelineTrackRow* row = resolve_timeline_graph_track(*state, tracks);
    if (row == nullptr) {
        ImGui::TextUnformatted(
            "Select a Transform or Slot Color track to view its scalar graph.");
        return stats;
    }

    const auto& projection = cached_timeline_graph_projection(state, *row);
    stats.status = projection.status;
    if (projection.status != timeline_graph_model::ProjectionStatus::Ready ||
        !projection.track.has_value()) {
        switch (projection.status) {
        case timeline_graph_model::ProjectionStatus::UnsupportedTrack:
            ImGui::TextUnformatted(
                "Select a Transform or Slot Color track to view its scalar graph.");
            break;
        case timeline_graph_model::ProjectionStatus::MissingSource:
            ImGui::TextUnformatted(
                "The focused graph track could not be resolved from the effective animation.");
            break;
        case timeline_graph_model::ProjectionStatus::InvalidData:
        case timeline_graph_model::ProjectionStatus::Ready:
            ImGui::TextUnformatted(
                "The focused graph track contains invalid or non-finite data.");
            break;
        }
        return stats;
    }

    const timeline_graph_model::Track& track = *projection.track;
    auto& graph_view = state->timeline_editor.graph_view;
    // Hiding the dragged component mid-gesture would leave an invisible drag
    // running against a frozen component index, so both graph drag kinds lock
    // the visibility controls for as long as they are live.
    const bool graph_drag_or_gesture_live =
        state->timeline_editor.graph_drag.has_value() ||
        state->timeline_editor.graph_value_gesture.has_value() ||
        state->timeline_editor.graph_handle_gesture.has_value();
    stats.component_controls_disabled = graph_drag_or_gesture_live;
    ImGui::BeginDisabled(graph_drag_or_gesture_live);
    bool had_visible_component = false;
    for (std::size_t component = 0U; component < track.components.size(); ++component) {
        had_visible_component = had_visible_component ||
            graph_view.component_visible[component];
    }
    for (std::size_t component = 0U; component < track.components.size(); ++component) {
        if (component != 0U) ImGui::SameLine();
        const auto descriptor = track.components[component];
        ImGui::PushStyleColor(
            ImGuiCol_Text,
            ImGui::ColorConvertU32ToFloat4(component_color(descriptor.component)));
        const bool component_changed = ImGui::Checkbox(
            component_label(descriptor.component),
            &graph_view.component_visible[component]);
        if (component == 0U) {
            const ImVec2 item_min = ImGui::GetItemRectMin();
            const ImVec2 item_max = ImGui::GetItemRectMax();
            stats.first_component_min_x = item_min.x;
            stats.first_component_min_y = item_min.y;
            stats.first_component_max_x = item_max.x;
            stats.first_component_max_y = item_max.y;
        }
        if (component_changed) {
            promote_displayed_fallback_focus(state, tracks, *row);
        }
        ImGui::PopStyleColor();
    }
    bool has_visible_component = false;
    for (std::size_t component = 0U; component < track.components.size(); ++component) {
        has_visible_component = has_visible_component ||
            graph_view.component_visible[component];
    }
    if (!had_visible_component && has_visible_component) graph_view.needs_fit = true;

    ImGui::EndDisabled();

    ImGui::SameLine();
    // The same shared TimelineEditorState::snap_to_frames field the Dopesheet
    // tab owns; toggling it in either tab is visible in the other.
    ImGui::Checkbox("Snap", &state->timeline_editor.snap_to_frames);
    ImGui::SameLine();
    ImGui::BeginDisabled(graph_drag_or_gesture_live);
    const bool fit_clicked = ImGui::SmallButton("Fit");
    const ImVec2 fit_item_min = ImGui::GetItemRectMin();
    const ImVec2 fit_item_max = ImGui::GetItemRectMax();
    stats.fit_min_x = fit_item_min.x;
    stats.fit_min_y = fit_item_min.y;
    stats.fit_max_x = fit_item_max.x;
    stats.fit_max_y = fit_item_max.y;
    ImGui::EndDisabled();
    ImGui::SameLine();
    // MAR-171 composes a mode clause on top of MAR-170's preset name.
    {
        std::string readout(
            outgoing_kind_label(track, state->timeline_editor.active_key));
        const auto mode = active_outgoing_curve_mode(*state, tracks);
        const auto driver = active_outgoing_curve_driver(*state, tracks);
        if (mode.has_value()) {
            if (*mode == marrow::editor::TimelineCurveMode::Auto) {
                readout += std::string(" \u00b7 Auto (") +
                    curve_driver_display_label(
                        driver.value_or(
                            marrow::editor::TimelineScalarComponent::Angle)) +
                    ")";
            } else {
                readout += " \u00b7 Manual";
            }
        }
        ImGui::TextDisabled("Outgoing: %s", readout.c_str());
    }

    if (track.components.size() == 1U) {
        ImGui::TextUnformatted(
            "Outgoing easing is shared by this key; dragging a handle edits that one curve.");
    } else {
        ImGui::TextUnformatted(
            "Outgoing easing is shared by every X/Y or RGBA component of this key; per-component curves are not supported.");
        ImGui::TextUnformatted(
            "Dragging a handle edits that one shared curve for every component of the key.");
    }
    ImGui::TextUnformatted(
        "A preset applies to every compatible selected key and, like a handle drag, writes each key's single shared easing.");
    ImGui::TextUnformatted(
        "An automatic curve is computed from the driver's own series and is still that key's one shared easing.");

    // MAR-170: appended after every existing widget, so no MAR-167/168/169
    // rectangle the actual-frame smokes aim at moves.
    draw_timeline_curve_preset_row(state, tracks, &stats);
    // MAR-171: appended after MAR-170's row, for the same reason.
    draw_timeline_curve_mode_row(state, tracks, &stats);

    const float total_width = std::max(160.0f, ImGui::GetContentRegionAvail().x);
    constexpr float kPlotHeight = 340.0f;
    constexpr float kLeftMargin = 66.0f;
    constexpr float kRightMargin = 8.0f;
    constexpr float kTopMargin = 8.0f;
    constexpr float kBottomMargin = 34.0f;
    ImGui::InvisibleButton(
        "timeline_graph_plot",
        ImVec2(total_width, kPlotHeight),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 item_min = ImGui::GetItemRectMin();
    const ImVec2 item_max = ImGui::GetItemRectMax();
    const timeline_graph_model::PlotRect plot{
        static_cast<double>(item_min.x + kLeftMargin),
        static_cast<double>(item_min.y + kTopMargin),
        static_cast<double>(std::max(item_min.x + kLeftMargin + 1.0f,
                                    item_max.x - kRightMargin)),
        static_cast<double>(std::max(item_min.y + kTopMargin + 1.0f,
                                    item_max.y - kBottomMargin))};
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(
        ImVec2(static_cast<float>(plot.min_x), static_cast<float>(plot.min_y)),
        ImVec2(static_cast<float>(plot.max_x), static_cast<float>(plot.max_y)),
        kGraphBackground);

    const ImGuiIO& io = ImGui::GetIO();
    const bool plot_item_hovered = ImGui::IsItemHovered();
    const bool plot_hovered = plot_item_hovered &&
        plot_contains(plot, io.MousePos.x, io.MousePos.y);
    if (plot_hovered) {
        ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
    }
    stats.plot_item_id = ImGui::GetItemID();
    stats.plot_min_x = static_cast<float>(plot.min_x);
    stats.plot_min_y = static_cast<float>(plot.min_y);
    stats.plot_max_x = static_cast<float>(plot.max_x);
    stats.plot_max_y = static_cast<float>(plot.max_y);
    stats.plot_item_hovered = plot_item_hovered;
    stats.plot_hovered = plot_hovered;
    const bool keyboard_fit = plot_hovered && !io.WantTextInput &&
        ImGui::IsKeyPressed(ImGuiKey_F, false);
    // The view transform is frozen for the whole drag: a mid-drag zoom, pan,
    // or auto-fit would make the pixel-to-unit mapping time-varying and could
    // teleport the dragged point.
    const bool drag_live = state->timeline_editor.graph_drag.has_value();
    if (!drag_live && (fit_clicked || keyboard_fit)) graph_view.needs_fit = true;

    if (!has_visible_component) {
        ImGui::SetCursorScreenPos(ImVec2(
            static_cast<float>(plot.min_x + 12.0),
            static_cast<float>(plot.min_y + 12.0)));
        ImGui::TextUnformatted("Enable at least one component");
        return stats;
    }

    if (!drag_live && graph_view.needs_fit && !fit_graph_view(state, track, plot)) {
        stats.status = timeline_graph_model::ProjectionStatus::InvalidData;
        ImGui::TextUnformatted(
            "The focused graph track contains invalid or non-finite data.");
        return stats;
    }

    if (!drag_live && plot_hovered && std::abs(io.MouseWheel) > 1e-6f) {
        if (io.KeyShift) {
            (void)timeline_graph_model::zoom_value_at(
                &graph_view.view, plot, io.MousePos.y, io.MouseWheel);
        } else {
            (void)timeline_graph_model::zoom_time_at(
                &graph_view.view, plot, io.MousePos.x, io.MouseWheel);
        }
    }
    if (!drag_live && ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        (void)timeline_graph_model::pan_view(
            &graph_view.view, io.MouseDelta.x, io.MouseDelta.y);
    }

    const auto geometry = timeline_graph_model::build_geometry(
        track,
        graph_view.component_visible,
        graph_view.view,
        plot,
        state->timeline_time_seconds);
    if (!geometry.has_value()) {
        stats.status = timeline_graph_model::ProjectionStatus::InvalidData;
        ImGui::TextUnformatted(
            "The focused graph track contains invalid or non-finite data.");
        return stats;
    }

    // Handles belong to the active key's outgoing segment, for one component.
    const auto handle_component_index =
        resolve_handle_component_index(track, graph_view);
    std::optional<timeline_graph_model::HandleGeometry> handles;
    if (handle_component_index.has_value() &&
        state->timeline_editor.active_key.has_value() &&
        state->timeline_editor.active_key->track_id == track.track_id) {
        handles = timeline_graph_model::build_handle_geometry(
            track,
            *state->timeline_editor.active_key,
            *handle_component_index,
            graph_view.view,
            plot);
    }
    if (handles.has_value()) {
        stats.handles_drawn = true;
        stats.first_handle_x = static_cast<float>(handles->first_handle.x);
        stats.first_handle_y = static_cast<float>(handles->first_handle.y);
        stats.second_handle_x = static_cast<float>(handles->second_handle.x);
        stats.second_handle_y = static_cast<float>(handles->second_handle.y);
        stats.handle_flat_value_span = handles->frame.flat_value_span;
        stats.active_segment_kind = handles->kind;
    }

    if (plot_hovered && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        // The handle hit test runs first: a handle press must not scrub the
        // playhead or change the selection, because that would move the very
        // anchors the frozen segment frame depends on.
        const auto handle_hit = handles.has_value()
            ? timeline_graph_model::hit_test_handle(
                  *handles,
                  static_cast<double>(io.MousePos.x),
                  static_cast<double>(io.MousePos.y),
                  kGraphHandleHitRadius)
            : std::nullopt;
        if (handle_hit.has_value()) {
            (void)begin_timeline_graph_handle_drag(
                state,
                *row,
                *handles,
                handle_hit->handle,
                ImGui::GetItemID(),
                plot,
                graph_view.view,
                static_cast<double>(io.MousePos.x),
                static_cast<double>(io.MousePos.y));
        } else if (const auto hit = timeline_graph_model::hit_test(
                       *geometry, io.MousePos.x, io.MousePos.y, 8.0);
                   hit.has_value()) {
            const bool additive = io.KeyCtrl || io.KeySuper;
            if (activate_timeline_graph_point(
                    state, *row, *hit, additive, "Timeline Graph")) {
                (void)begin_timeline_graph_point_drag(
                    state,
                    *row,
                    *hit,
                    ImGui::GetItemID(),
                    plot,
                    graph_view.view,
                    static_cast<double>(io.MousePos.x),
                    static_cast<double>(io.MousePos.y));
            }
        } else {
            const auto selection = state->timeline_editor.selected_keys;
            const auto active_key = state->timeline_editor.active_key;
            promote_displayed_fallback_focus(state, tracks, *row);
            const double time = timeline_graph_model::time_at_x(
                plot, graph_view.view, static_cast<double>(io.MousePos.x));
            state->timeline_playing = false;
            (void)scrub_timeline_time(state, std::max(0.0, time), "Timeline Graph", true);
            state->timeline_editor.selected_keys = selection;
            state->timeline_editor.active_key = active_key;
        }
    }
    std::string drag_readout = timeline_graph_drag_readout(*state, track.kind);
    if (drag_readout.empty()) drag_readout = timeline_graph_easing_readout(*state);
    stats.handle_gesture_active =
        state->timeline_editor.graph_handle_gesture.has_value();
    stats.drag_active = state->timeline_editor.graph_drag.has_value();
    stats.value_gesture_active =
        state->timeline_editor.graph_value_gesture.has_value();
    stats.graph_owns_retime =
        stats.drag_active && state->timeline_editor.retime_gesture.has_value();
    stats.drag_axis = state->timeline_editor.graph_drag.has_value()
        ? state->timeline_editor.graph_drag->axis
        : timeline_graph_model::DragAxis::Undecided;

    draw_time_ticks(
        draw_list, plot, graph_view.view,
        state->timeline_editor.frames_per_second);
    const ValueTickDrawStats value_tick_stats =
        draw_value_ticks(draw_list, plot, graph_view.view, track.kind);
    stats.zero_value_tick_label_count = value_tick_stats.zero_label_count;
    stats.one_value_tick_label_count = value_tick_stats.one_label_count;

    draw_list->PushClipRect(
        ImVec2(static_cast<float>(plot.min_x), static_cast<float>(plot.min_y)),
        ImVec2(static_cast<float>(plot.max_x), static_cast<float>(plot.max_y)),
        true);
    for (const auto& segment : geometry->segments) {
        if (segment.polyline.size() < 2U) continue;
        std::vector<ImVec2> polyline;
        polyline.reserve(segment.polyline.size());
        for (const auto& point : segment.polyline) {
            polyline.emplace_back(
                static_cast<float>(point.x), static_cast<float>(point.y));
        }
        const ImU32 color = component_color(segment.component);
        draw_list->AddPolyline(
            polyline.data(), static_cast<int>(polyline.size()), color,
            ImDrawFlags_None, 2.0f);
        const ImVec2 marker(
            static_cast<float>(segment.marker.x),
            static_cast<float>(segment.marker.y));
        switch (segment.kind) {
        case timeline_graph_model::SegmentKind::Linear:
            ++stats.linear_segment_count;
            draw_list->AddLine(
                ImVec2(marker.x, marker.y - 3.0f),
                ImVec2(marker.x, marker.y + 3.0f), color, 1.0f);
            break;
        case timeline_graph_model::SegmentKind::Stepped:
            ++stats.stepped_segment_count;
            draw_list->AddRect(
                ImVec2(marker.x - 3.0f, marker.y - 3.0f),
                ImVec2(marker.x + 3.0f, marker.y + 3.0f), color);
            break;
        case timeline_graph_model::SegmentKind::Cubic:
            ++stats.cubic_segment_count;
            draw_list->AddCircle(marker, 3.5f, color, 12, 1.0f);
            break;
        }
    }
    if (handles.has_value()) {
        const ImVec2 start_anchor(
            static_cast<float>(handles->start_anchor.x),
            static_cast<float>(handles->start_anchor.y));
        const ImVec2 end_anchor(
            static_cast<float>(handles->end_anchor.x),
            static_cast<float>(handles->end_anchor.y));
        const ImVec2 first(
            static_cast<float>(handles->first_handle.x),
            static_cast<float>(handles->first_handle.y));
        const ImVec2 second(
            static_cast<float>(handles->second_handle.x),
            static_cast<float>(handles->second_handle.y));
        // MAR-171: an automatic segment's handles are hollow amber. They stay
        // fully grabbable, and grabbing one demotes the segment to manual.
        const bool automatic = stats.active_key_auto;
        draw_list->AddLine(
            start_anchor, first,
            automatic ? kAutoHandleTangent : kGraphHandleTangent, 1.0f);
        draw_list->AddLine(
            end_anchor, second,
            automatic ? kAutoHandleTangent : kGraphHandleTangent, 1.0f);
        for (const ImVec2& handle : {first, second}) {
            const ImVec2 top_left(
                handle.x - kGraphHandleHalfExtent, handle.y - kGraphHandleHalfExtent);
            const ImVec2 bottom_right(
                handle.x + kGraphHandleHalfExtent, handle.y + kGraphHandleHalfExtent);
            if (automatic) {
                draw_list->AddRect(top_left, bottom_right, kAutoHandleStroke);
            } else {
                draw_list->AddRectFilled(top_left, bottom_right, kGraphHandleFill);
            }
        }
    }
    if (geometry->playhead_x.has_value()) {
        const float x = static_cast<float>(*geometry->playhead_x);
        draw_list->AddLine(
            ImVec2(x, static_cast<float>(plot.min_y)),
            ImVec2(x, static_cast<float>(plot.max_y)),
            kGraphPlayhead,
            1.0f);
        stats.playhead_drawn = true;
    }
    for (const auto& point : geometry->points) {
        const ImVec2 position(
            static_cast<float>(point.position.x),
            static_cast<float>(point.position.y));
        draw_list->AddCircleFilled(position, 5.0f, component_color(point.component), 16);
        ++stats.point_count;
        if (!stats.first_point_valid) {
            stats.first_point_x = position.x;
            stats.first_point_y = position.y;
            stats.first_point_valid = true;
        }
        if (state->timeline_editor.active_key.has_value() &&
            *state->timeline_editor.active_key == point.key &&
            graph_view.active_component == point.component) {
            stats.active_point_x = position.x;
            stats.active_point_y = position.y;
            stats.active_point_valid = true;
        }
        if (key_is_selected(*state, point.key)) {
            draw_list->AddCircle(position, 7.0f, kGraphSelection, 16, 2.0f);
        }
        if (state->timeline_editor.active_key.has_value() &&
            *state->timeline_editor.active_key == point.key &&
            graph_view.active_component == point.component) {
            draw_list->AddCircleFilled(position, 1.75f, kGraphActiveCenter, 8);
        }
    }
    draw_list->PopClipRect();
    draw_list->AddRect(
        ImVec2(static_cast<float>(plot.min_x), static_cast<float>(plot.min_y)),
        ImVec2(static_cast<float>(plot.max_x), static_cast<float>(plot.max_y)),
        kGraphGrid);

    if (!drag_readout.empty()) {
        ImGui::TextUnformatted(drag_readout.c_str());
    }
    return stats;
}

} // namespace marrow::editor::shell
