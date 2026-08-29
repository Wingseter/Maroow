#include "shell_timeline_graph.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>

#include "imgui.h"

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

double value_from_y(
    timeline_graph_model::PlotRect rect,
    const timeline_graph_model::View& view,
    double y) {
    return view.value_center +
        (((rect.min_y + rect.max_y) * 0.5) - y) / view.pixels_per_value;
}

double y_from_value(
    timeline_graph_model::PlotRect rect,
    const timeline_graph_model::View& view,
    double value) {
    return (rect.min_y + rect.max_y) * 0.5 -
        (value - view.value_center) * view.pixels_per_value;
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
    const double top_value = value_from_y(rect, view, rect.min_y);
    const double bottom_value = value_from_y(rect, view, rect.max_y);
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
                draw_list, rect, y_from_value(rect, view, value), value, decimals)) {
            if (tick_values_coincide(value, 0.0, *step)) ++stats.zero_label_count;
            if (tick_values_coincide(value, 1.0, *step)) ++stats.one_label_count;
        }
    }
    if (kind == timeline_graph_model::TrackKind::SlotColor) {
        if (draw_value_label(
                draw_list, rect, y_from_value(rect, view, 0.0), 0.0, decimals)) {
            ++stats.zero_label_count;
        }
        if (draw_value_label(
                draw_list, rect, y_from_value(rect, view, 1.0), 1.0, decimals)) {
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
    switch (it->outgoing_easing.kind()) {
    case marrow::runtime::InterpolationKind::Linear: return "Linear";
    case marrow::runtime::InterpolationKind::Stepped: return "Stepped";
    case marrow::runtime::InterpolationKind::CubicBezier: return "Cubic";
    }
    return "No outgoing segment";
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

std::array<bool, 4> available_components(
    const timeline_graph_model::Projection& projection) {
    std::array<bool, 4> visible{false, false, false, false};
    if (!projection.track.has_value()) return visible;
    const std::size_t count = std::min(visible.size(), projection.track->components.size());
    std::fill_n(visible.begin(), count, true);
    return visible;
}

} // namespace

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

    ImGui::SameLine();
    const bool fit_clicked = ImGui::SmallButton("Fit");
    const ImVec2 fit_item_min = ImGui::GetItemRectMin();
    const ImVec2 fit_item_max = ImGui::GetItemRectMax();
    stats.fit_min_x = fit_item_min.x;
    stats.fit_min_y = fit_item_min.y;
    stats.fit_max_x = fit_item_max.x;
    stats.fit_max_y = fit_item_max.y;
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Outgoing: %s",
        outgoing_kind_label(track, state->timeline_editor.active_key));

    if (track.components.size() == 1U) {
        ImGui::TextUnformatted("Outgoing easing is shared by this key.");
    } else {
        ImGui::TextUnformatted(
            "Outgoing easing is shared by every X/Y or RGBA component of this key; per-component curves are not supported.");
    }

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
    if (fit_clicked || keyboard_fit) graph_view.needs_fit = true;

    if (!has_visible_component) {
        ImGui::SetCursorScreenPos(ImVec2(
            static_cast<float>(plot.min_x + 12.0),
            static_cast<float>(plot.min_y + 12.0)));
        ImGui::TextUnformatted("Enable at least one component");
        return stats;
    }

    if (graph_view.needs_fit && !fit_graph_view(state, track, plot)) {
        stats.status = timeline_graph_model::ProjectionStatus::InvalidData;
        ImGui::TextUnformatted(
            "The focused graph track contains invalid or non-finite data.");
        return stats;
    }

    if (plot_hovered && std::abs(io.MouseWheel) > 1e-6f) {
        if (io.KeyShift) {
            (void)timeline_graph_model::zoom_value_at(
                &graph_view.view, plot, io.MousePos.y, io.MouseWheel);
        } else {
            (void)timeline_graph_model::zoom_time_at(
                &graph_view.view, plot, io.MousePos.x, io.MouseWheel);
        }
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
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

    const ImGuiID pressed_point_storage_id =
        ImGui::GetID("timeline_graph_pressed_point");
    ImGuiStorage* storage = ImGui::GetStateStorage();
    if (plot_hovered && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        const auto hit = timeline_graph_model::hit_test(
            *geometry, io.MousePos.x, io.MousePos.y, 8.0);
        if (hit.has_value()) {
            const bool additive = io.KeyCtrl || io.KeySuper;
            (void)activate_timeline_graph_point(
                state, *row, *hit, additive, "Timeline Graph");
            storage->SetInt(pressed_point_storage_id, 1);
        } else {
            const auto selection = state->timeline_editor.selected_keys;
            const auto active_key = state->timeline_editor.active_key;
            promote_displayed_fallback_focus(state, tracks, *row);
            const double time = graph_view.view.view_start_seconds +
                (static_cast<double>(io.MousePos.x) - plot.min_x) /
                    graph_view.view.pixels_per_second;
            state->timeline_playing = false;
            (void)scrub_timeline_time(state, std::max(0.0, time), "Timeline Graph", true);
            state->timeline_editor.selected_keys = selection;
            state->timeline_editor.active_key = active_key;
            storage->SetInt(pressed_point_storage_id, 0);
        }
    }
    const bool point_drag_notice = ImGui::IsItemActive() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left) &&
        storage->GetInt(pressed_point_storage_id, 0) != 0;
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        storage->SetInt(pressed_point_storage_id, 0);
    }

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

    if (point_drag_notice) {
        ImGui::TextUnformatted("Value/time dragging is available in MAR-168");
    }
    return stats;
}

} // namespace marrow::editor::shell
