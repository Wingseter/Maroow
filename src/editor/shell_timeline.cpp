#include "shell_timeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "imgui.h"

#include "shell_coalesced_edit.hpp"
#include "shell_derived_cache.hpp"
#include "shell_selection.hpp"
#include "shell_preview.hpp"
#include "shell_state.hpp"
#include "shell_widgets.hpp"
#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/editor/authoring.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;
using marrow::editor::IconRegistry;
using marrow::editor::timeline_model::clamp_existing_key_time;
using marrow::editor::timeline_model::clamp_existing_non_decreasing_key_time;

namespace {

template <typename MutateFn>
bool apply_timeline_project_drag(
    ShellState* state,
    bool changed,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    std::string failure_status,
    MutateFn mutate) {
    return apply_coalesced_edit_frame(
        state,
        coalesced_edit_frame_from_last_item(changed),
        CoalescedEditDescriptor{
            kind,
            std::move(label),
            std::move(group),
            allow_merge,
            CoalescedEditPolicy::ProjectRuntime,
            std::move(failure_status)},
        std::move(mutate));
}

} // namespace


marrow::editor::Icon track_property_icon(const std::string& track_id) {
    if (track_id.find(":Rotate") != std::string::npos)      return Icon::PropRotate;
    if (track_id.find(":Translate") != std::string::npos)   return Icon::PropTranslate;
    if (track_id.find(":Scale") != std::string::npos)       return Icon::PropScale;
    if (track_id.find(":Shear") != std::string::npos)       return Icon::PropShear;
    if (track_id.find(":Color") != std::string::npos)       return Icon::PropColor;
    if (track_id.find(":Attachment") != std::string::npos)  return Icon::AttRegion;
    if (track_id.find(":deform:") != std::string::npos)     return Icon::AttMesh;
    if (track_id.find("draw_order") != std::string::npos)   return Icon::PropOrder;
    if (track_id.find("event") != std::string::npos)        return Icon::PropEvent;
    return Icon::NodeBone;
}

// A muted left-edge accent grouping tracks by property family. Stays within
// the restrained v2 palette: transform family → primary-dim, colour → the
// tertiary accent, structural (attachment/order/event) → secondary. Used as a
// 2px rail so unselected lanes still read as grouped without new hues.
ImU32 track_group_accent(const std::string& track_id) {
    if (track_id.find(":Rotate") != std::string::npos ||
        track_id.find(":Translate") != std::string::npos ||
        track_id.find(":Scale") != std::string::npos ||
        track_id.find(":Shear") != std::string::npos) {
        return IM_COL32(0x52, 0x69, 0xa8, 0x80);  // primary-dim @ 50%
    }
    if (track_id.find(":Color") != std::string::npos) {
        return IM_COL32(0xa8, 0x46, 0x43, 0x80);  // tertiary-dim @ 50%
    }
    return IM_COL32(0xbe, 0xc7, 0xdc, 0x4D);      // secondary @ 30%
}

double timeline_major_tick_interval(double pixels_per_second) {
    constexpr std::array<double, 12> kIntervals{
        1.0 / 60.0, 1.0 / 30.0, 0.05, 0.1, 0.25, 0.5,
        1.0, 2.0, 5.0, 10.0, 30.0, 60.0};
    for (const double interval : kIntervals) {
        if (interval * pixels_per_second >= 72.0) return interval;
    }
    return kIntervals.back();
}

double timeline_time_from_x(
    const ShellState& state,
    float screen_x,
    float lane_min_x) {
    return state.timeline_editor.view_start_seconds +
        static_cast<double>(screen_x - lane_min_x) /
            state.timeline_editor.pixels_per_second;
}

float timeline_x_from_time(
    const ShellState& state,
    double time_seconds,
    float lane_min_x) {
    return lane_min_x + static_cast<float>(
        (time_seconds - state.timeline_editor.view_start_seconds) *
        state.timeline_editor.pixels_per_second);
}


void update_timeline_retime_gesture(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || !state->timeline_editor.retime_gesture.has_value()) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        finish_timeline_retime_gesture(state, false);
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        finish_timeline_retime_gesture(state, true);
        return;
    }
    TimelineRetimeGesture& gesture = *state->timeline_editor.retime_gesture;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) return;
    const double requested_delta =
        static_cast<double>(ImGui::GetIO().MousePos.x - gesture.start_mouse_x) /
        state->timeline_editor.pixels_per_second;
    const bool snap_to_frames =
        state->timeline_editor.snap_to_frames && !ImGui::GetIO().KeyAlt;
    (void)apply_timeline_retime_delta(
        state, tracks, requested_delta, snap_to_frames);
}

void draw_timeline_ruler(ShellState* state, double duration_seconds) {
    const float width = std::max(96.0f, ImGui::GetContentRegionAvail().x);
    constexpr float kHeight = 28.0f;
    ImGui::InvisibleButton(
        "timeline_ruler",
        ImVec2(width, kHeight),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(0x17, 0x1a, 0x21, 0xff));

    if (ImGui::IsItemHovered() && std::abs(ImGui::GetIO().MouseWheel) > 1e-6f) {
        const double cursor_time = timeline_time_from_x(
            *state, ImGui::GetIO().MousePos.x, rect_min.x);
        const double zoom_factor = std::pow(1.15, ImGui::GetIO().MouseWheel);
        state->timeline_editor.pixels_per_second = std::clamp(
            state->timeline_editor.pixels_per_second * zoom_factor, 24.0, 1600.0);
        state->timeline_editor.view_start_seconds = std::max(
            0.0,
            cursor_time -
                static_cast<double>(ImGui::GetIO().MousePos.x - rect_min.x) /
                    state->timeline_editor.pixels_per_second);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        state->timeline_editor.view_start_seconds = std::max(
            0.0,
            state->timeline_editor.view_start_seconds -
                static_cast<double>(ImGui::GetIO().MouseDelta.x) /
                    state->timeline_editor.pixels_per_second);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state->timeline_playing = false;
        scrub_timeline_time(
            state,
            timeline_time_from_x(*state, ImGui::GetIO().MousePos.x, rect_min.x),
            "Timeline ruler",
            true);
    }

    const double interval = timeline_major_tick_interval(
        state->timeline_editor.pixels_per_second);
    const double visible_end = state->timeline_editor.view_start_seconds +
        static_cast<double>(rect_max.x - rect_min.x) /
            state->timeline_editor.pixels_per_second;
    double tick_time = std::floor(state->timeline_editor.view_start_seconds / interval) * interval;
    for (; tick_time <= visible_end + interval; tick_time += interval) {
        if (tick_time < 0.0) continue;
        const float x = timeline_x_from_time(*state, tick_time, rect_min.x);
        draw_list->AddLine(
            ImVec2(x, rect_min.y + 10.0f),
            ImVec2(x, rect_max.y),
            IM_COL32(0x76, 0x79, 0x86, 0xb0));
        char label[48]{};
        std::snprintf(
            label,
            sizeof(label),
            "%.2fs  f%d",
            tick_time,
            static_cast<int>(std::llround(tick_time * state->timeline_editor.frames_per_second)));
        draw_list->AddText(ImVec2(x + 3.0f, rect_min.y + 1.0f), IM_COL32_WHITE, label);
    }
    const float playhead_x =
        timeline_x_from_time(*state, state->timeline_time_seconds, rect_min.x);
    if (playhead_x >= rect_min.x && playhead_x <= rect_max.x) {
        draw_list->AddLine(
            ImVec2(playhead_x, rect_min.y),
            ImVec2(playhead_x, rect_max.y),
            IM_COL32(0xff, 0x54, 0x50, 0xff),
            2.0f);
    }
    ImGui::SetItemTooltip(
        "%.0f FPS | mouse wheel zoom | middle-drag pan | span %.3fs",
        state->timeline_editor.frames_per_second,
        duration_seconds);
}

void draw_timeline_lane(
    ShellState* state,
    const TimelineTrackRow& track,
    const std::vector<TimelineTrackRow>& tracks) {
    ImGui::PushID(track.id.c_str());

    const bool selected = timeline_track_matches_selection(*state, track);
    const float lane_height = 24.0f;
    const float lane_width = std::max(96.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("timeline_lane", ImVec2(lane_width, lane_height));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    const float rect_width = std::max(1.0f, rect_max.x - rect_min.x);

    // Charcoal Studio lane: transparent when unselected (table zebra shows
    // through), subtle primary tint when selected. No opaque border — ghost
    // 20% outline_variant instead.
    if (selected) {
        draw_list->AddRectFilled(
            rect_min,
            rect_max,
            IM_COL32(0x60, 0x8b, 0xff, 0x1F),  // primary-container @ ~12%
            2.0f);
        draw_list->AddLine(
            ImVec2(rect_min.x, rect_min.y),
            ImVec2(rect_min.x, rect_max.y),
            IM_COL32(0x60, 0x8b, 0xff, 0xFF),  // primary-container solid
            2.0f);
    } else {
        // Property-group accent rail (muted; selection overrides it above).
        draw_list->AddLine(
            ImVec2(rect_min.x, rect_min.y),
            ImVec2(rect_min.x, rect_max.y),
            track_group_accent(track.id),
            2.0f);
    }

    const double major_interval = timeline_major_tick_interval(
        state->timeline_editor.pixels_per_second);
    const double visible_end = state->timeline_editor.view_start_seconds +
        static_cast<double>(rect_width) / state->timeline_editor.pixels_per_second;
    double tick_time =
        std::floor(state->timeline_editor.view_start_seconds / major_interval) * major_interval;
    for (; tick_time <= visible_end + major_interval; tick_time += major_interval) {
        const float tick_x = timeline_x_from_time(*state, tick_time, rect_min.x);
        draw_list->AddLine(
            ImVec2(tick_x, rect_min.y + 2.0f),
            ImVec2(tick_x, rect_max.y - 2.0f),
            IM_COL32(0x43, 0x46, 0x54, 0x4D));  // outline_variant @ 30%
    }

    // Horizontal interpolation hint line connecting keyframes
    if (track.key_times.size() >= 2) {
        const float line_y = (rect_min.y + rect_max.y) * 0.5f;
        draw_list->AddLine(
            ImVec2(timeline_x_from_time(*state, track.key_times.front(), rect_min.x), line_y),
            ImVec2(timeline_x_from_time(*state, track.key_times.back(), rect_min.x), line_y),
            IM_COL32(0xb3, 0xc5, 0xff, 0x26));  // primary @ ~15%
    }

    // Playhead: tertiary-container red (#ff5450)
    {
        const float playhead_x =
            timeline_x_from_time(*state, state->timeline_time_seconds, rect_min.x);
        draw_list->AddLine(
            ImVec2(playhead_x, rect_min.y),
            ImVec2(playhead_x, rect_max.y),
            IM_COL32(0xff, 0x54, 0x50, 0xFF),
            1.0f);
    }

    // Keyframe diamonds: primary normally, tertiary-container at playhead,
    // secondary on unselected rows for hierarchy.
    const float marker_half = 4.0f;
    for (std::size_t key_index = 0; key_index < track.key_times.size(); ++key_index) {
        const double key_time = track.key_times[key_index];
        const float marker_x = timeline_x_from_time(*state, key_time, rect_min.x);
        if (marker_x < rect_min.x - marker_half || marker_x > rect_max.x + marker_half) continue;
        const bool near_playhead =
            std::abs(key_time - state->timeline_time_seconds) <= 1e-6;
        const bool key_selected = timeline_key_selected(
            *state, timeline_key_ref(track, key_index));
        ImU32 fill_color;
        if (key_selected) {
            fill_color = IM_COL32(0xff, 0xc1, 0x5c, 0xff);
        } else if (near_playhead) {
            fill_color = IM_COL32(0xff, 0x54, 0x50, 0xFF);   // tertiary-container
        } else if (selected) {
            fill_color = IM_COL32(0xb3, 0xc5, 0xff, 0xFF);   // primary
        } else {
            fill_color = IM_COL32(0xbe, 0xc7, 0xdc, 0xD0);   // secondary @ 82%
        }
        const ImU32 outline_color = IM_COL32(0x10, 0x13, 0x19, 0xFF);  // surface
        const float cx = marker_x;
        const float cy = (rect_min.y + rect_max.y) * 0.5f;
        draw_list->AddQuadFilled(
            ImVec2(cx,                cy - marker_half),
            ImVec2(cx + marker_half,  cy),
            ImVec2(cx,                cy + marker_half),
            ImVec2(cx - marker_half,  cy),
            fill_color);
        draw_list->AddQuad(
            ImVec2(cx,                cy - marker_half),
            ImVec2(cx + marker_half,  cy),
            ImVec2(cx,                cy + marker_half),
            ImVec2(cx - marker_half,  cy),
            outline_color,
            1.0f);
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !state->timeline_editor.retime_gesture.has_value()) {
        const float local_x = std::clamp(ImGui::GetIO().MousePos.x - rect_min.x, 0.0f, rect_width);
        const double clicked_time = timeline_time_from_x(
            *state, rect_min.x + local_x, rect_min.x);
        const double threshold_time = 9.0 / state->timeline_editor.pixels_per_second;
        std::optional<std::size_t> hit_key_index;
        double hit_distance = threshold_time;
        for (std::size_t index = 0; index < track.key_times.size(); ++index) {
            const double distance = std::abs(track.key_times[index] - clicked_time);
            if (distance <= hit_distance) {
                hit_distance = distance;
                hit_key_index = index;
            }
        }
        const bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (hit_key_index.has_value()) {
            const TimelineKeyRef hit = timeline_key_ref(track, *hit_key_index);
            if (activate_timeline_key(
                    state, track, *hit_key_index, additive, "Timeline", true) &&
                timeline_key_selected(*state, hit) && timeline_track_is_editable(track)) {
                begin_timeline_retime_gesture(
                    state,
                    ImGui::GetItemID(),
                    ImGui::GetIO().MousePos.x,
                    tracks);
            }
        } else {
            state->timeline_playing = false;
            focus_timeline_track(state, track, clicked_time, "Timeline", true);
            if (!additive) {
                state->timeline_editor.selected_keys.clear();
                state->timeline_editor.active_key.reset();
            }
            state->timeline_editor.box_selection = TimelineBoxSelection{
                track.id, clicked_time, clicked_time, additive};
        }
    }

    if (state->timeline_editor.box_selection.has_value() &&
        state->timeline_editor.box_selection->track_id == track.id) {
        auto& box = *state->timeline_editor.box_selection;
        box.current_time = timeline_time_from_x(
            *state,
            std::clamp(ImGui::GetIO().MousePos.x, rect_min.x, rect_max.x),
            rect_min.x);
        const float box_start = timeline_x_from_time(*state, box.start_time, rect_min.x);
        const float box_end = timeline_x_from_time(*state, box.current_time, rect_min.x);
        draw_list->AddRectFilled(
            ImVec2(std::min(box_start, box_end), rect_min.y),
            ImVec2(std::max(box_start, box_end), rect_max.y),
            IM_COL32(0x60, 0x8b, 0xff, 0x30));
        draw_list->AddRect(
            ImVec2(std::min(box_start, box_end), rect_min.y),
            ImVec2(std::max(box_start, box_end), rect_max.y),
            IM_COL32(0xb3, 0xc5, 0xff, 0xc0));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const double minimum_time = std::min(box.start_time, box.current_time);
            const double maximum_time = std::max(box.start_time, box.current_time);
            auto& selection = state->timeline_editor.selected_keys;
            if (!box.additive) selection.clear();
            for (std::size_t index = 0; index < track.key_times.size(); ++index) {
                if (track.key_times[index] < minimum_time - 1e-6 ||
                    track.key_times[index] > maximum_time + 1e-6) continue;
                const TimelineKeyRef key = timeline_key_ref(track, index);
                if (!timeline_key_selected(*state, key)) selection.push_back(key);
            }
            state->timeline_editor.box_selection.reset();
            reconcile_timeline_key_selection(state, tracks);
        }
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "%s\nKeys: %zu",
            track.label.c_str(),
            track.key_times.size());
    }

    ImGui::PopID();
}

void copy_string_to_input_buffer(
    std::string_view source,
    std::array<char, 256>* buffer) {
    if (buffer == nullptr) {
        return;
    }

    buffer->fill('\0');
    const std::size_t copy_size = std::min(source.size(), buffer->size() - 1U);
    std::snprintf(buffer->data(), buffer->size(), "%.*s", static_cast<int>(copy_size), source.data());
}

void draw_draw_order_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || state->load_result.project == nullptr) {
        ImGui::TextUnformatted("Load a valid project to edit draw order keys.");
        return;
    }

    ImGui::TextUnformatted("Draw Order Key Editor");
    const auto runtime_edit = make_draw_order_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected draw-order track could not be resolved.");
        return;
    }

    const marrow::editor::DrawOrderTimelineEdit* existing_edit =
        state->load_result.project->find_draw_order_timeline_edit(track.animation_name);
    const marrow::editor::DrawOrderTimelineEdit display_edit =
        existing_edit != nullptr ? *existing_edit : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);

    ImGui::Text("%s / Global / Draw Order", track.animation_name.c_str());
    ImGui::Text(
        "Source: %s",
        existing_edit != nullptr ? "project draw-order edit" : "runtime track");
    ImGui::TextUnformatted(
        "Each key lists the full slot stack. Move entries up or down to preview reordered presentation.");

    const auto track_group = [&]() {
        return std::string("timeline:") + track.id;
    };
    const auto key_group = [&](std::size_t key_index) {
        return track_group() + ":key:" + std::to_string(key_index);
    };
    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message,
                                           EditActionKind kind = EditActionKind::EditProperty,
                                           std::string group = {},
                                           bool allow_merge = true) {
        if (group.empty()) {
            group = track_group();
        }
        if (!apply_project_command_change(
                state,
                previous_project,
                kind,
                std::move(status_message),
                std::move(group),
                allow_merge,
                "Draw-order edit failed")) {
            return false;
        }

        state->selected_timeline_track_id = track.id;
        return true;
    };

    if (ImGui::Button("Add Key At Playhead##draw_order")) {
        const marrow::editor::ProjectData previous_project = *state->load_result.project;
        const auto edit_index = ensure_draw_order_timeline_edit_index(state, track);
        if (edit_index.has_value()) {
            auto& editable_track =
                state->load_result.project->draw_order_timeline_edits[*edit_index];
            if (const auto insert_time = insertable_key_time(
                    editable_track.keyframes,
                    state->timeline_time_seconds,
                    duration_seconds)) {
                marrow::editor::DrawOrderKeyframeEdit new_key =
                    sample_draw_order_keyframe(*state);
                new_key.time = *insert_time;
                const auto iterator = std::upper_bound(
                    editable_track.keyframes.begin(),
                    editable_track.keyframes.end(),
                    new_key.time,
                    [](double time, const marrow::editor::DrawOrderKeyframeEdit& keyframe) {
                        return time < keyframe.time;
                    });
                editable_track.keyframes.insert(iterator, std::move(new_key));
                commit_project_change(
                    previous_project,
                    "Added a draw-order key on " + track.animation_name,
                    EditActionKind::AddKeyframe,
                    track_group(),
                    false);
            } else {
                *state->load_result.project = previous_project;
                state->status_message =
                    "Could not place a new draw-order key between existing keyframes";
            }
        }
    }

    ImGui::BeginChild("draw_order_key_editor", ImVec2(0.0f, 280.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto& display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (display_edit.keyframes.size() > 1U &&
                ImGui::Button("Remove Key##draw_order")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_draw_order_timeline_edit_index(state, track)) {
                    auto& editable_track =
                        state->load_result.project->draw_order_timeline_edits[*edit_index];
                    editable_track.keyframes.erase(
                        editable_track.keyframes.begin() + static_cast<std::ptrdiff_t>(key_index));
                    commit_project_change(
                        previous_project,
                        "Removed a draw-order key on " + track.animation_name,
                        EditActionKind::RemoveKeyframe,
                        track_group(),
                        false);
                }
            }

            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time",
                ImGuiDataType_Double,
                &edited_time,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated draw-order key timing on " + track.animation_name,
                key_group(key_index),
                false,
                "Draw-order edit failed",
                [&]() {
                    if (const auto edit_index =
                            ensure_draw_order_timeline_edit_index(state, track)) {
                        auto& editable_track =
                            state->load_result.project->draw_order_timeline_edits[*edit_index];
                        editable_track.keyframes[key_index].time = clamp_existing_key_time(
                            editable_track.keyframes,
                            key_index,
                            edited_time,
                            duration_seconds);
                    }
                });

            if (ImGui::Button("Use Current Preview Order")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_draw_order_timeline_edit_index(state, track)) {
                    state->load_result.project->draw_order_timeline_edits[*edit_index]
                        .keyframes[key_index]
                        .slot_names = sample_draw_order_keyframe(*state).slot_names;
                    commit_project_change(
                        previous_project,
                        "Copied the preview slot order into " + track.animation_name);
                }
            }

            ImGui::Separator();
            for (std::size_t slot_order_index = 0;
                 slot_order_index < display_key.slot_names.size();
                 ++slot_order_index) {
                ImGui::PushID(static_cast<int>(slot_order_index));
                ImGui::Text(
                    "%zu. %s",
                    slot_order_index + 1U,
                    display_key.slot_names[slot_order_index].c_str());
                if (slot_order_index > 0U) {
                    ImGui::SameLine();
                    if (ImGui::Button("Up")) {
                        const marrow::editor::ProjectData previous_project =
                            *state->load_result.project;
                        if (const auto edit_index =
                                ensure_draw_order_timeline_edit_index(state, track)) {
                            auto& slot_names =
                                state->load_result.project->draw_order_timeline_edits[*edit_index]
                                    .keyframes[key_index]
                                    .slot_names;
                            std::swap(slot_names[slot_order_index], slot_names[slot_order_index - 1U]);
                            commit_project_change(
                                previous_project,
                                "Moved " + slot_names[slot_order_index - 1U] +
                                    " earlier in the draw order");
                        }
                    }
                }
                if (slot_order_index + 1U < display_key.slot_names.size()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Down")) {
                        const marrow::editor::ProjectData previous_project =
                            *state->load_result.project;
                        if (const auto edit_index =
                                ensure_draw_order_timeline_edit_index(state, track)) {
                            auto& slot_names =
                                state->load_result.project->draw_order_timeline_edits[*edit_index]
                                    .keyframes[key_index]
                                    .slot_names;
                            std::swap(slot_names[slot_order_index], slot_names[slot_order_index + 1U]);
                            commit_project_change(
                                previous_project,
                                "Moved " + slot_names[slot_order_index + 1U] +
                                    " later in the draw order");
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_event_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || state->load_result.project == nullptr) {
        ImGui::TextUnformatted("Load a valid project to edit event keys.");
        return;
    }
    if (state->load_result.skeleton_data->events().empty()) {
        ImGui::TextUnformatted("The loaded skeleton does not define any event names.");
        return;
    }

    ImGui::TextUnformatted("Event Key Editor");
    const auto runtime_edit = make_event_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected event track could not be resolved.");
        return;
    }

    const marrow::editor::EventTimelineEdit* existing_edit =
        state->load_result.project->find_event_timeline_edit(track.animation_name);
    const marrow::editor::EventTimelineEdit display_edit =
        existing_edit != nullptr ? *existing_edit : *runtime_edit;

    ImGui::Text("%s / Global / Events", track.animation_name.c_str());
    ImGui::Text(
        "Source: %s",
        existing_edit != nullptr ? "project event edit" : "runtime track");
    ImGui::TextUnformatted(
        "Preview playback emits the resolved event payloads up to the current playhead.");

    if (state->preview_events.empty()) {
        ImGui::TextDisabled("Triggered at preview time: none");
    } else {
        ImGui::TextUnformatted("Triggered at preview time:");
        for (const auto& event : state->preview_events) {
            ImGui::BulletText(
                "%s @ %.3fs  int=%d  float=%.2f  string=%s",
                event.name.c_str(),
                event.time,
                event.int_value,
                event.float_value,
                event.string_value.c_str());
        }
    }

    const auto event_track_group = [&]() {
        return std::string("timeline:") + track.id;
    };
    const auto event_key_group = [&](std::size_t key_index) {
        return event_track_group() + ":key:" + std::to_string(key_index);
    };
    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message,
                                           EditActionKind kind = EditActionKind::EditProperty,
                                           std::string group = {},
                                           bool allow_merge = true) {
        if (group.empty()) {
            group = event_track_group();
        }
        if (!apply_project_command_change(
                state,
                previous_project,
                kind,
                std::move(status_message),
                std::move(group),
                allow_merge,
                "Event edit failed")) {
            return false;
        }

        state->selected_timeline_track_id = track.id;
        return true;
    };

    if (ImGui::Button("Add Key At Playhead##events")) {
        const marrow::editor::ProjectData previous_project = *state->load_result.project;
        if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
            auto& editable_track =
                state->load_result.project->event_timeline_edits[*edit_index];
            marrow::editor::EventKeyframeEdit new_key = sample_event_keyframe(*state);
            const auto iterator = std::upper_bound(
                editable_track.keyframes.begin(),
                editable_track.keyframes.end(),
                new_key.time,
                [](double time, const marrow::editor::EventKeyframeEdit& keyframe) {
                    return time < keyframe.time;
                });
            editable_track.keyframes.insert(iterator, std::move(new_key));
            commit_project_change(
                previous_project,
                "Added an event key on " + track.animation_name,
                EditActionKind::AddKeyframe,
                event_track_group(),
                false);
        }
    }

    ImGui::BeginChild("event_key_editor", ImVec2(0.0f, 340.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto& display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time) + " / " + display_key.event_name;
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (display_edit.keyframes.size() > 1U &&
                ImGui::Button("Remove Key##events")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                    auto& editable_track =
                        state->load_result.project->event_timeline_edits[*edit_index];
                    editable_track.keyframes.erase(
                        editable_track.keyframes.begin() + static_cast<std::ptrdiff_t>(key_index));
                    commit_project_change(
                        previous_project,
                        "Removed an event key on " + track.animation_name,
                        EditActionKind::RemoveKeyframe,
                        event_track_group(),
                        false);
                }
            }

            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time",
                ImGuiDataType_Double,
                &edited_time,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated event key timing on " + track.animation_name,
                event_key_group(key_index),
                false,
                "Event edit failed",
                [&]() {
                    if (const auto edit_index =
                            ensure_event_timeline_edit_index(state, track)) {
                        auto& editable_track =
                            state->load_result.project->event_timeline_edits[*edit_index];
                        editable_track.keyframes[key_index].time =
                            clamp_existing_non_decreasing_key_time(
                                editable_track.keyframes,
                                key_index,
                                edited_time);
                    }
                });

            if (ImGui::BeginCombo("Event", display_key.event_name.c_str())) {
                for (const auto& definition : state->load_result.skeleton_data->events()) {
                    const bool selected = display_key.event_name == definition.name;
                    if (ImGui::Selectable(definition.name.c_str(), selected)) {
                        const marrow::editor::ProjectData previous_project =
                            *state->load_result.project;
                        if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                            state->load_result.project->event_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .event_name = definition.name;
                            commit_project_change(
                                previous_project,
                                "Updated event key name on " + track.animation_name);
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            const auto event_definition = std::find_if(
                state->load_result.skeleton_data->events().begin(),
                state->load_result.skeleton_data->events().end(),
                [&](const marrow::runtime::EventDefinition& definition) {
                    return definition.name == display_key.event_name;
                });
            if (event_definition != state->load_result.skeleton_data->events().end()) {
                ImGui::TextDisabled(
                    "Defaults: int=%d float=%.2f string=%s",
                    event_definition->int_value,
                    event_definition->float_value,
                    event_definition->string_value.c_str());
            }

            auto update_optional_number = [&](const char* toggle_label,
                                              const char* value_label,
                                              std::optional<double> value,
                                              double default_value,
                                              auto setter,
                                              std::string toggle_status,
                                              std::string value_status) {
                bool enabled = value.has_value();
                if (ImGui::Checkbox(toggle_label, &enabled)) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        setter(
                            &state->load_result.project->event_timeline_edits[*edit_index]
                                 .keyframes[key_index],
                            enabled ? std::optional<double>(default_value) : std::nullopt);
                        commit_project_change(
                            previous_project,
                            std::move(toggle_status),
                            EditActionKind::EditProperty,
                            event_key_group(key_index),
                            true);
                    }
                }
                if (!enabled) {
                    return;
                }

                double edited_value = value.value_or(default_value);
                const bool value_changed = ImGui::DragScalar(
                    value_label,
                    ImGuiDataType_Double,
                    &edited_value,
                    0.05f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    value_changed,
                    EditActionKind::EditProperty,
                    std::move(value_status),
                    event_key_group(key_index),
                    false,
                    "Event edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_event_timeline_edit_index(state, track)) {
                            setter(
                                &state->load_result.project->event_timeline_edits[*edit_index]
                                     .keyframes[key_index],
                                std::optional<double>(edited_value));
                        }
                    });
            };

            bool override_int = display_key.int_value.has_value();
            if (ImGui::Checkbox("Override Int", &override_int)) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                    auto& key =
                        state->load_result.project->event_timeline_edits[*edit_index]
                            .keyframes[key_index];
                    key.int_value = override_int
                        ? std::optional<int>(
                              event_definition != state->load_result.skeleton_data->events().end()
                                  ? event_definition->int_value
                                  : 0)
                        : std::nullopt;
                    commit_project_change(previous_project, "Updated event int override");
                }
            }
            if (override_int) {
                int edited_value = display_key.int_value.value_or(
                    event_definition != state->load_result.skeleton_data->events().end()
                        ? event_definition->int_value
                        : 0);
                if (ImGui::InputInt("Int", &edited_value)) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        state->load_result.project->event_timeline_edits[*edit_index]
                            .keyframes[key_index]
                            .int_value = edited_value;
                        commit_project_change(previous_project, "Updated event int value");
                    }
                }
            }

            update_optional_number(
                "Override Float",
                "Float",
                display_key.float_value,
                event_definition != state->load_result.skeleton_data->events().end()
                    ? event_definition->float_value
                    : 0.0,
                [](marrow::editor::EventKeyframeEdit* key, std::optional<double> value) {
                    key->float_value = value;
                },
                "Updated event float override",
                "Updated event float value");
            update_optional_number(
                "Override Volume",
                "Volume",
                display_key.volume,
                event_definition != state->load_result.skeleton_data->events().end()
                    ? event_definition->volume
                    : 1.0,
                [](marrow::editor::EventKeyframeEdit* key, std::optional<double> value) {
                    key->volume = value;
                },
                "Updated event volume override",
                "Updated event volume value");
            update_optional_number(
                "Override Balance",
                "Balance",
                display_key.balance,
                event_definition != state->load_result.skeleton_data->events().end()
                    ? event_definition->balance
                    : 0.0,
                [](marrow::editor::EventKeyframeEdit* key, std::optional<double> value) {
                    key->balance = value;
                },
                "Updated event balance override",
                "Updated event balance value");

            const auto update_optional_text = [&](const char* toggle_label,
                                                  const char* value_label,
                                                  const std::optional<std::string>& value,
                                                  std::string_view default_value,
                                                  auto setter,
                                                  std::string status) {
                bool enabled = value.has_value();
                if (ImGui::Checkbox(toggle_label, &enabled)) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        setter(
                            &state->load_result.project->event_timeline_edits[*edit_index]
                                 .keyframes[key_index],
                            enabled ? std::optional<std::string>(default_value) : std::nullopt);
                        commit_project_change(previous_project, status);
                    }
                }
                if (!enabled) {
                    return;
                }

                std::array<char, 256> buffer{};
                copy_string_to_input_buffer(value.value_or(std::string(default_value)), &buffer);
                if (ImGui::InputText(value_label, buffer.data(), buffer.size())) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        setter(
                            &state->load_result.project->event_timeline_edits[*edit_index]
                                 .keyframes[key_index],
                            std::optional<std::string>(buffer.data()));
                        commit_project_change(previous_project, status);
                    }
                }
            };

            update_optional_text(
                "Override String",
                "String",
                display_key.string_value,
                event_definition != state->load_result.skeleton_data->events().end()
                    ? std::string_view(event_definition->string_value)
                    : std::string_view{},
                [](marrow::editor::EventKeyframeEdit* key,
                   std::optional<std::string> value) {
                    key->string_value = std::move(value);
                },
                "Updated event string override");
            update_optional_text(
                "Override Audio",
                "Audio",
                display_key.audio_path,
                event_definition != state->load_result.skeleton_data->events().end() &&
                        event_definition->audio_path.has_value()
                    ? std::string_view(*event_definition->audio_path)
                    : std::string_view{},
                [](marrow::editor::EventKeyframeEdit* key,
                   std::optional<std::string> value) {
                    key->audio_path = std::move(value);
                },
                "Updated event audio override");
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_slot_color_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || !track.slot_index.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        ImGui::TextUnformatted("The selected slot-color track could not be resolved.");
        return;
    }
    const std::string& slot_name =
        state->load_result.skeleton_data->slots()[*track.slot_index].name;
    const auto runtime_edit = make_slot_color_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected slot-color track could not be resolved.");
        return;
    }
    const auto* existing = state->load_result.project->find_slot_color_timeline_edit(
        track.animation_name, slot_name);
    const marrow::editor::SlotColorTimelineEdit display_edit =
        existing != nullptr ? *existing : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);

    ImGui::TextUnformatted("Slot Light RGBA Key Editor");
    ImGui::Text("%s / %s / Light RGBA", track.animation_name.c_str(), slot_name.c_str());
    ImGui::TextDisabled("Dark tint remains read-only. Light color and alpha export to .mskl.");
    ImGui::BeginChild("slot_color_key_editor", ImVec2(0.0f, 270.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time", ImGuiDataType_Double, &edited_time, 0.01f, nullptr, nullptr, "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated slot-color key timing on " + slot_name,
                "timeline:" + track.id + ":key:" + std::to_string(key_index),
                false,
                "Slot-color edit failed",
                [&]() {
                    if (const auto edit_index = ensure_slot_color_timeline_edit_index(state, track)) {
                        auto& keys = state->load_result.project
                            ->slot_color_timeline_edits[*edit_index].keyframes;
                        keys[key_index].time = clamp_existing_key_time(
                            keys, key_index, edited_time, duration_seconds);
                    }
                });

            float rgba[4] = {
                display_key.color.r,
                display_key.color.g,
                display_key.color.b,
                display_key.color.a};
            const bool color_changed = ImGui::ColorEdit4(
                "Light RGBA", rgba, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float);
            apply_timeline_project_drag(
                state,
                color_changed,
                EditActionKind::EditProperty,
                "Updated slot light color on " + slot_name,
                "timeline:" + track.id + ":key:" + std::to_string(key_index),
                false,
                "Slot-color edit failed",
                [&]() {
                    if (const auto edit_index = ensure_slot_color_timeline_edit_index(state, track)) {
                        auto& key = state->load_result.project
                            ->slot_color_timeline_edits[*edit_index].keyframes[key_index];
                        key.color = marrow::runtime::SlotColor{
                            std::clamp(rgba[0], 0.0f, 1.0f),
                            std::clamp(rgba[1], 0.0f, 1.0f),
                            std::clamp(rgba[2], 0.0f, 1.0f),
                            std::clamp(rgba[3], 0.0f, 1.0f)};
                    }
                });

            int interpolation_kind = 0;
            switch (display_key.interpolation.kind()) {
            case marrow::runtime::InterpolationKind::Linear:
                interpolation_kind = 0;
                break;
            case marrow::runtime::InterpolationKind::Stepped:
                interpolation_kind = 1;
                break;
            case marrow::runtime::InterpolationKind::CubicBezier:
                interpolation_kind = 2;
                break;
            }
            constexpr const char* kInterpolationLabels[] = {"Linear", "Stepped", "Bezier"};
            if (ImGui::Combo(
                    "Interpolation",
                    &interpolation_kind,
                    kInterpolationLabels,
                    IM_ARRAYSIZE(kInterpolationLabels))) {
                const marrow::editor::ProjectData previous = *state->load_result.project;
                if (const auto edit_index = ensure_slot_color_timeline_edit_index(state, track)) {
                    auto& interpolation = state->load_result.project
                        ->slot_color_timeline_edits[*edit_index].keyframes[key_index].interpolation;
                    interpolation = interpolation_kind == 0
                        ? marrow::runtime::Interpolation::linear()
                        : interpolation_kind == 1
                            ? marrow::runtime::Interpolation::stepped()
                            : marrow::runtime::Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9);
                    apply_project_command_change(
                        state,
                        previous,
                        EditActionKind::EditProperty,
                        "Updated slot-color interpolation on " + slot_name,
                        "timeline:" + track.id + ":key:" + std::to_string(key_index),
                        false,
                        "Slot-color edit failed");
                }
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_slot_attachment_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || !track.slot_index.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        ImGui::TextUnformatted("The selected attachment track could not be resolved.");
        return;
    }
    const auto& skeleton = *state->load_result.skeleton_data;
    const std::string slot_name = skeleton.slots()[*track.slot_index].name;
    const auto runtime_edit = make_slot_attachment_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected attachment track could not be resolved.");
        return;
    }
    const auto* existing = state->load_result.project->find_slot_attachment_timeline_edit(
        track.animation_name, slot_name);
    const marrow::editor::SlotAttachmentTimelineEdit display_edit =
        existing != nullptr ? *existing : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);
    const std::vector<std::string>& attachment_names =
        cached_timeline_attachment_names(state, *track.slot_index);

    ImGui::TextUnformatted("Slot Attachment Key Editor");
    ImGui::Text("%s / %s / Attachment", track.animation_name.c_str(), slot_name.c_str());
    ImGui::TextDisabled("Attachment keys are stepped; <none> hides the slot.");
    ImGui::BeginChild("slot_attachment_key_editor", ImVec2(0.0f, 250.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time", ImGuiDataType_Double, &edited_time, 0.01f, nullptr, nullptr, "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated attachment key timing on " + slot_name,
                "timeline:" + track.id + ":key:" + std::to_string(key_index),
                false,
                "Attachment edit failed",
                [&]() {
                    if (const auto edit_index =
                            ensure_slot_attachment_timeline_edit_index(state, track)) {
                        auto& keys = state->load_result.project
                            ->slot_attachment_timeline_edits[*edit_index].keyframes;
                        keys[key_index].time = clamp_existing_key_time(
                            keys, key_index, edited_time, duration_seconds);
                    }
                });

            const char* selected_name = display_key.attachment_name.has_value()
                ? display_key.attachment_name->c_str()
                : "<none>";
            if (ImGui::BeginCombo("Attachment", selected_name)) {
                const auto choose_attachment = [&](std::optional<std::string> name) {
                    const marrow::editor::ProjectData previous = *state->load_result.project;
                    if (const auto edit_index =
                            ensure_slot_attachment_timeline_edit_index(state, track)) {
                        state->load_result.project
                            ->slot_attachment_timeline_edits[*edit_index]
                            .keyframes[key_index]
                            .attachment_name = std::move(name);
                        apply_project_command_change(
                            state,
                            previous,
                            EditActionKind::EditProperty,
                            "Updated attachment key on " + slot_name,
                            "timeline:" + track.id + ":key:" + std::to_string(key_index),
                            false,
                            "Attachment edit failed");
                    }
                };
                if (ImGui::Selectable("<none>", !display_key.attachment_name.has_value())) {
                    choose_attachment(std::nullopt);
                }
                for (const std::string& attachment_name : attachment_names) {
                    const bool selected =
                        display_key.attachment_name == std::optional<std::string>(attachment_name);
                    if (ImGui::Selectable(attachment_name.c_str(), selected)) {
                        choose_attachment(attachment_name);
                    }
                }
                ImGui::EndCombo();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_transform_timeline_editor(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    ImGui::Separator();

    if (!state->load_result || state->load_result.project == nullptr) {
        ImGui::TextUnformatted("Load a valid project to edit transform keys.");
        return;
    }

    const TimelineTrackRow* track = selected_timeline_track(*state, tracks);
    if (track == nullptr) {
        ImGui::TextUnformatted("Select a transform track row to author keyed motion.");
        return;
    }
    if (track->id == "global:draw-order") {
        draw_draw_order_timeline_editor(state, *track);
        return;
    }
    if (track->id == "global:events") {
        draw_event_timeline_editor(state, *track);
        return;
    }
    if (track->deform_attachment_name.has_value()) {
        draw_mesh_deform_timeline_editor(state, *track);
        return;
    }
    if (track->slot_index.has_value() && track->id.find(":Color") != std::string::npos) {
        draw_slot_color_timeline_editor(state, *track);
        return;
    }
    if (track->slot_index.has_value() && track->id.find(":Attachment") != std::string::npos) {
        draw_slot_attachment_timeline_editor(state, *track);
        return;
    }

    ImGui::TextUnformatted("Transform Key Editor");
    if (!track->transform_channel.has_value() || !track->bone_index.has_value() ||
        *track->bone_index >= state->load_result.skeleton_data->bones().size()) {
        ImGui::TextUnformatted(
            "The selected timeline row is read-only. Keyframe editing is available for rotate, translate, scale, and shear tracks.");
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    const std::string& bone_name = skeleton.bones()[*track->bone_index].name;
    const auto runtime_edit = make_transform_timeline_edit(*state, *track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected transform track could not be resolved.");
        return;
    }

    const marrow::editor::TransformTimelineEdit* existing_edit =
        state->load_result.project->find_transform_timeline_edit(
            track->animation_name,
            bone_name,
            *track->transform_channel);
    const marrow::editor::TransformTimelineEdit display_edit =
        existing_edit != nullptr ? *existing_edit : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);

    ImGui::Text(
        "%s / %s / %s",
        track->animation_name.c_str(),
        bone_name.c_str(),
        std::string(transform_channel_label(*track->transform_channel)).c_str());
    ImGui::Text(
        "Source: %s",
        existing_edit != nullptr ? "project timeline edit" : "runtime track");
    ImGui::TextUnformatted(
        "Edits are stored in the .marrow project file and exported back into a .mskl skeleton.");

    const auto track_group = [&]() {
        return std::string("timeline:") + track->id;
    };
    const auto key_group = [&](std::size_t key_index) {
        return track_group() + ":key:" + std::to_string(key_index);
    };
    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           EditActionKind kind,
                                           std::string group,
                                           bool allow_merge,
                                           std::string status_message) {
        if (!apply_project_command_change(
                state,
                previous_project,
                kind,
                std::move(status_message),
                std::move(group),
                allow_merge,
                "Timeline edit failed")) {
            return false;
        }

        state->selected_timeline_track_id = track->id;
        return true;
    };

    if (ImGui::Button("Add Key At Playhead")) {
        const auto sampled = sample_transform_keyframe(*state, *track);
        namespace json = marrow::runtime::json;
        json::Value::Object cmd_obj;
        cmd_obj.emplace("op", json::Value("set_transform", {}));
        json::Value::Object args_obj;
        args_obj.emplace("animation", json::Value(track->animation_name, {}));
        args_obj.emplace("bone", json::Value(bone_name, {}));
        
        const char* channel_name = "rotate";
        switch (*track->transform_channel) {
            case marrow::editor::TransformTimelineChannel::Rotate: channel_name = "rotate"; break;
            case marrow::editor::TransformTimelineChannel::Translate: channel_name = "translate"; break;
            case marrow::editor::TransformTimelineChannel::Scale: channel_name = "scale"; break;
            case marrow::editor::TransformTimelineChannel::Shear: channel_name = "shear"; break;
        }
        args_obj.emplace("channel", json::Value(channel_name, {}));
        args_obj.emplace("time", json::Value(state->timeline_time_seconds, {}));
        if (*track->transform_channel == marrow::editor::TransformTimelineChannel::Rotate) {
            args_obj.emplace("angle", json::Value(sampled.angle, {}));
        } else {
            args_obj.emplace("x", json::Value(sampled.x, {}));
            args_obj.emplace("y", json::Value(sampled.y, {}));
        }
        cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
        dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
    }

    ImGui::BeginChild("transform_key_editor", ImVec2(0.0f, 250.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto& display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(
                header.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen)) {
            if (display_edit.keyframes.size() > 1U &&
                ImGui::Button("Remove Key##transform")) {
                namespace json = marrow::runtime::json;
                json::Value::Object cmd_obj;
                cmd_obj.emplace("op", json::Value("remove_transform_keyframe", {}));
                json::Value::Object args_obj;
                args_obj.emplace("animation", json::Value(track->animation_name, {}));
                args_obj.emplace("bone", json::Value(bone_name, {}));
                
                const char* channel_name = "rotate";
                switch (*track->transform_channel) {
                    case marrow::editor::TransformTimelineChannel::Rotate: channel_name = "rotate"; break;
                    case marrow::editor::TransformTimelineChannel::Translate: channel_name = "translate"; break;
                    case marrow::editor::TransformTimelineChannel::Scale: channel_name = "scale"; break;
                    case marrow::editor::TransformTimelineChannel::Shear: channel_name = "shear"; break;
                }
                args_obj.emplace("channel", json::Value(channel_name, {}));
                args_obj.emplace("time", json::Value(display_key.time, {}));
                cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
                dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
            }

            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time",
                ImGuiDataType_Double,
                &edited_time,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated key timing on " + bone_name + " " +
                    std::string(transform_channel_label(*track->transform_channel)),
                key_group(key_index),
                false,
                "Timeline edit failed",
                [&]() {
                    const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                    if (edit_index.has_value()) {
                        auto& editable_track =
                            state->load_result.project->transform_timeline_edits[*edit_index];
                        editable_track.keyframes[key_index].time = clamp_existing_key_time(
                            editable_track.keyframes,
                            key_index,
                            edited_time,
                            duration_seconds);
                    }
                });

            if (*track->transform_channel == marrow::editor::TransformTimelineChannel::Rotate) {
                const double setup_rotation = static_cast<double>(
                    skeleton.bones()[*track->bone_index].setup_pose.rotation);
                double edited_angle = display_key.angle + setup_rotation;
                const bool angle_changed = ImGui::DragScalar(
                    "Angle",
                    ImGuiDataType_Double,
                    &edited_angle,
                    0.1f,
                    nullptr,
                    nullptr,
                    "%.3f deg");
                apply_timeline_project_drag(
                    state,
                    angle_changed,
                    EditActionKind::MoveBone,
                    "Updated key angle on " + bone_name,
                    key_group(key_index),
                    false,
                    "Timeline edit failed",
                    [&]() {
                        const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                        if (edit_index.has_value()) {
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .angle = marrow::editor::setup_relative_rotation_key(
                                    *state->session.runtime_data(),
                                    bone_name,
                                    edited_angle);
                        }
                    });
            } else {
                double edited_x = display_key.x;
                const bool x_changed = ImGui::DragScalar(
                    "X",
                    ImGuiDataType_Double,
                    &edited_x,
                    0.1f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    x_changed,
                    EditActionKind::MoveBone,
                    "Updated key X on " + bone_name,
                    key_group(key_index),
                    false,
                    "Timeline edit failed",
                    [&]() {
                        const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                        if (edit_index.has_value()) {
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .x = edited_x;
                        }
                    });

                double edited_y = display_key.y;
                const bool y_changed = ImGui::DragScalar(
                    "Y",
                    ImGuiDataType_Double,
                    &edited_y,
                    0.1f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    y_changed,
                    EditActionKind::MoveBone,
                    "Updated key Y on " + bone_name,
                    key_group(key_index),
                    false,
                    "Timeline edit failed",
                    [&]() {
                        const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                        if (edit_index.has_value()) {
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .y = edited_y;
                        }
                    });
            }

            int interpolation_kind = 0;
            switch (display_key.interpolation.kind()) {
            case marrow::runtime::InterpolationKind::Linear:
                interpolation_kind = 0;
                break;
            case marrow::runtime::InterpolationKind::Stepped:
                interpolation_kind = 1;
                break;
            case marrow::runtime::InterpolationKind::CubicBezier:
                interpolation_kind = 2;
                break;
            }
            constexpr const char* kInterpolationLabels[] = {
                "Linear",
                "Stepped",
                "Bezier",
            };
            if (ImGui::Combo(
                    "Interpolation",
                    &interpolation_kind,
                    kInterpolationLabels,
                    IM_ARRAYSIZE(kInterpolationLabels))) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                if (edit_index.has_value()) {
                    auto& editable_key =
                        state->load_result.project->transform_timeline_edits[*edit_index]
                            .keyframes[key_index];
                    switch (interpolation_kind) {
                    case 0:
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::linear();
                        break;
                    case 1:
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::stepped();
                        break;
                    case 2: {
                        marrow::runtime::CubicBezierControlPoints bezier{
                            0.25,
                            0.1,
                            0.75,
                            0.9,
                        };
                        if (display_key.interpolation.kind() ==
                            marrow::runtime::InterpolationKind::CubicBezier) {
                            bezier = display_key.interpolation.cubic_bezier();
                        }
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                static_cast<double>(bezier.cx1),
                                static_cast<double>(bezier.cy1),
                                static_cast<double>(bezier.cx2),
                                static_cast<double>(bezier.cy2));
                        break;
                    }
                    }
                    commit_project_change(
                        previous_project,
                        EditActionKind::EditProperty,
                        key_group(key_index),
                        true,
                        "Updated interpolation on " + bone_name + " " +
                            std::string(transform_channel_label(*track->transform_channel)));
                }
            }

            if (display_key.interpolation.kind() ==
                marrow::runtime::InterpolationKind::CubicBezier) {
                marrow::runtime::CubicBezierControlPoints bezier =
                    display_key.interpolation.cubic_bezier();

                const auto update_bezier = [&](const char* label,
                                               marrow::runtime::AnimationScalar* component,
                                               bool clamp_x,
                                               std::string status) {
                    const bool changed = ImGui::DragScalar(
                        label,
                        ImGuiDataType_Float,
                        component,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f");
                    if (clamp_x) {
                        *component = std::clamp(*component, 0.0f, 1.0f);
                    }
                    apply_timeline_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        key_group(key_index),
                        false,
                        "Timeline edit failed",
                        [&]() {
                            const auto edit_index =
                                ensure_transform_timeline_edit_index(state, *track);
                            if (edit_index.has_value()) {
                                auto& editable_key =
                                    state->load_result.project
                                        ->transform_timeline_edits[*edit_index]
                                        .keyframes[key_index];
                                editable_key.interpolation =
                                    marrow::runtime::Interpolation::cubic_bezier(
                                        static_cast<double>(bezier.cx1),
                                        static_cast<double>(bezier.cy1),
                                        static_cast<double>(bezier.cx2),
                                        static_cast<double>(bezier.cy2));
                            }
                        });
                };
                update_bezier("Bezier X1", &bezier.cx1, true, "Updated bezier control point X1");
                update_bezier("Bezier Y1", &bezier.cy1, false, "Updated bezier control point Y1");
                update_bezier("Bezier X2", &bezier.cx2, true, "Updated bezier control point X2");
                update_bezier("Bezier Y2", &bezier.cy2, false, "Updated bezier control point Y2");
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_mesh_deform_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || state->load_result.project == nullptr ||
        !track.slot_index.has_value() || !track.deform_attachment_name.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        ImGui::TextUnformatted("The selected deform track could not be resolved.");
        return;
    }

    ImGui::TextUnformatted("Mesh Deform Key Editor");

    const auto& skeleton = *state->load_result.skeleton_data;
    const std::string& slot_name = skeleton.slots()[*track.slot_index].name;
    const auto runtime_edit = make_mesh_deform_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected deform track could not be resolved.");
        return;
    }

    const auto* attachment = skeleton.find_attachment_source(
        *track.slot_index, *track.deform_attachment_name);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        ImGui::TextUnformatted("The selected deform track no longer resolves to a mesh attachment.");
        return;
    }

    const marrow::editor::MeshDeformTimelineEdit* existing_edit =
        state->load_result.project->find_mesh_deform_timeline_edit(
            track.animation_name,
            slot_name,
            *track.deform_attachment_name);
    const marrow::editor::MeshDeformTimelineEdit display_edit =
        existing_edit != nullptr ? *existing_edit : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);
    const std::size_t vertex_count = attachment->mesh_geometry->vertices.size() / 2U;

    ImGui::Text(
        "%s / %s / %s",
        track.animation_name.c_str(),
        slot_name.c_str(),
        track.deform_attachment_name->c_str());
    ImGui::Text(
        "Source: %s",
        existing_edit != nullptr ? "project deform edit" : "runtime deform track");
    ImGui::Text(
        "Vertices: %zu  Components per key: %zu",
        vertex_count,
        attachment->mesh_geometry->vertices.size());
    ImGui::TextUnformatted(
        "Offsets are authored per vertex in local mesh space and exported back into the .mskl deform timeline.");

    const auto deform_track_group = [&]() {
        return std::string("timeline:") + track.id;
    };
    const auto deform_key_group = [&](std::size_t key_index) {
        return deform_track_group() + ":key:" + std::to_string(key_index);
    };
    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message,
                                           EditActionKind kind = EditActionKind::EditProperty,
                                           std::string group = {},
                                           bool allow_merge = true) {
        if (group.empty()) {
            group = deform_track_group();
        }
        if (!apply_project_command_change(
                state,
                previous_project,
                kind,
                std::move(status_message),
                std::move(group),
                allow_merge,
                "Mesh deform edit failed")) {
            return false;
        }

        state->selected_timeline_track_id = track.id;
        return true;
    };

    if (ImGui::Button("Add Key At Playhead")) {
        const marrow::editor::ProjectData previous_project = *state->load_result.project;
        const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
        if (edit_index.has_value()) {
            auto& editable_track =
                state->load_result.project->mesh_deform_timeline_edits[*edit_index];
            if (const auto insert_time = insertable_key_time(
                    editable_track.keyframes,
                    state->timeline_time_seconds,
                    duration_seconds)) {
                marrow::editor::DeformKeyframeEdit new_key =
                    sample_deform_keyframe(*state, track);
                new_key.time = *insert_time;
                const auto iterator = std::upper_bound(
                    editable_track.keyframes.begin(),
                    editable_track.keyframes.end(),
                    new_key.time,
                    [](double time, const marrow::editor::DeformKeyframeEdit& keyframe) {
                        return time < keyframe.time;
                    });
                editable_track.keyframes.insert(iterator, std::move(new_key));
                commit_project_change(
                    previous_project,
                    "Added a mesh deform key on " + slot_name + " / " +
                        *track.deform_attachment_name,
                    EditActionKind::AddKeyframe,
                    deform_track_group(),
                    false);
            } else {
                *state->load_result.project = previous_project;
                state->status_message = "Could not place a new deform key between existing keyframes";
            }
        }
    }

    ImGui::BeginChild("mesh_deform_key_editor", ImVec2(0.0f, 300.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto& display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (display_edit.keyframes.size() > 1U &&
                ImGui::Button("Remove Key##deform")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track)) {
                    auto& editable_track =
                        state->load_result.project->mesh_deform_timeline_edits[*edit_index];
                    editable_track.keyframes.erase(
                        editable_track.keyframes.begin() + static_cast<std::ptrdiff_t>(key_index));
                    commit_project_change(
                        previous_project,
                        "Removed a mesh deform key on " + slot_name + " / " +
                            *track.deform_attachment_name,
                        EditActionKind::RemoveKeyframe,
                        deform_track_group(),
                        false);
                }
            }

            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time",
                ImGuiDataType_Double,
                &edited_time,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated deform key timing on " + slot_name,
                deform_key_group(key_index),
                false,
                "Mesh deform edit failed",
                [&]() {
                    const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                    if (edit_index.has_value()) {
                        auto& editable_track =
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index];
                        editable_track.keyframes[key_index].time = clamp_existing_key_time(
                            editable_track.keyframes,
                            key_index,
                            edited_time,
                            duration_seconds);
                    }
                });

            int interpolation_kind = 0;
            switch (display_key.interpolation.kind()) {
            case marrow::runtime::InterpolationKind::Linear:
                interpolation_kind = 0;
                break;
            case marrow::runtime::InterpolationKind::Stepped:
                interpolation_kind = 1;
                break;
            case marrow::runtime::InterpolationKind::CubicBezier:
                interpolation_kind = 2;
                break;
            }
            constexpr const char* kInterpolationLabels[] = {
                "Linear",
                "Stepped",
                "Bezier",
            };
            if (ImGui::Combo(
                    "Interpolation",
                    &interpolation_kind,
                    kInterpolationLabels,
                    IM_ARRAYSIZE(kInterpolationLabels))) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                if (edit_index.has_value()) {
                    auto& editable_key =
                        state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                            .keyframes[key_index];
                    switch (interpolation_kind) {
                    case 0:
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::linear();
                        break;
                    case 1:
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::stepped();
                        break;
                    case 2: {
                        marrow::runtime::CubicBezierControlPoints bezier{
                            0.25,
                            0.1,
                            0.75,
                            0.9,
                        };
                        if (display_key.interpolation.kind() ==
                            marrow::runtime::InterpolationKind::CubicBezier) {
                            bezier = display_key.interpolation.cubic_bezier();
                        }
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                static_cast<double>(bezier.cx1),
                                static_cast<double>(bezier.cy1),
                                static_cast<double>(bezier.cx2),
                                static_cast<double>(bezier.cy2));
                        break;
                    }
                    }
                    commit_project_change(
                        previous_project,
                        "Updated deform interpolation on " + slot_name);
                }
            }

            if (display_key.interpolation.kind() ==
                marrow::runtime::InterpolationKind::CubicBezier) {
                marrow::runtime::CubicBezierControlPoints bezier =
                    display_key.interpolation.cubic_bezier();

                const auto update_bezier = [&](const char* label,
                                               marrow::runtime::AnimationScalar* component,
                                               bool clamp_x,
                                               std::string status) {
                    const bool changed = ImGui::DragScalar(
                        label,
                        ImGuiDataType_Float,
                        component,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f");
                    if (clamp_x) {
                        *component = std::clamp(*component, 0.0f, 1.0f);
                    }
                    apply_timeline_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        deform_key_group(key_index),
                        false,
                        "Mesh deform edit failed",
                        [&]() {
                            const auto edit_index =
                                ensure_mesh_deform_timeline_edit_index(state, track);
                            if (edit_index.has_value()) {
                                auto& editable_key =
                                    state->load_result.project
                                        ->mesh_deform_timeline_edits[*edit_index]
                                        .keyframes[key_index];
                                editable_key.interpolation =
                                    marrow::runtime::Interpolation::cubic_bezier(
                                        static_cast<double>(bezier.cx1),
                                        static_cast<double>(bezier.cy1),
                                        static_cast<double>(bezier.cx2),
                                        static_cast<double>(bezier.cy2));
                            }
                        });
                };
                update_bezier("Bezier X1", &bezier.cx1, true, "Updated deform bezier control point X1");
                update_bezier("Bezier Y1", &bezier.cy1, false, "Updated deform bezier control point Y1");
                update_bezier("Bezier X2", &bezier.cx2, true, "Updated deform bezier control point X2");
                update_bezier("Bezier Y2", &bezier.cy2, false, "Updated deform bezier control point Y2");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Vertex Offsets");
            for (std::size_t vertex_index = 0; vertex_index < display_key.vertex_offsets.size() / 2U;
                 ++vertex_index) {
                ImGui::PushID(static_cast<int>(vertex_index));
                const std::size_t x_index = vertex_index * 2U;
                const std::size_t y_index = x_index + 1U;

                double edited_x = display_key.vertex_offsets[x_index];
                const bool x_changed = ImGui::DragScalar(
                    "X",
                    ImGuiDataType_Double,
                    &edited_x,
                    0.25f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    x_changed,
                    EditActionKind::EditProperty,
                    "Updated deform vertex X on " + slot_name + " / " +
                        *track.deform_attachment_name,
                    deform_key_group(key_index) + ":vertex:" + std::to_string(vertex_index),
                    false,
                    "Mesh deform edit failed",
                    [&]() {
                        const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                        if (edit_index.has_value()) {
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .vertex_offsets[x_index] = edited_x;
                        }
                    });

                ImGui::SameLine();
                double edited_y = display_key.vertex_offsets[y_index];
                const bool y_changed = ImGui::DragScalar(
                    "Y",
                    ImGuiDataType_Double,
                    &edited_y,
                    0.25f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    y_changed,
                    EditActionKind::EditProperty,
                    "Updated deform vertex Y on " + slot_name + " / " +
                        *track.deform_attachment_name,
                    deform_key_group(key_index) + ":vertex:" + std::to_string(vertex_index),
                    false,
                    "Mesh deform edit failed",
                    [&]() {
                        const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                        if (edit_index.has_value()) {
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .vertex_offsets[y_index] = edited_y;
                        }
                    });

                ImGui::SameLine();
                ImGui::TextDisabled(
                    "V%zu  base(%.1f, %.1f)",
                    vertex_index,
                    attachment->mesh_geometry->vertices[x_index],
                    attachment->mesh_geometry->vertices[y_index]);
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

static void draw_dopesheet_body(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double duration_seconds) {
    const TimelineTrackRow* toolbar_track = selected_timeline_track(*state, tracks);
    const bool has_editable_track =
        toolbar_track != nullptr && timeline_track_is_editable(*toolbar_track);
    if (icon_button(
            state->icons,
            Icon::AddKey,
            has_editable_track
                ? "Add or replace a keyframe at the playhead"
                : "Select an editable track (Inherit remains read-only)",
            false,
            !has_editable_track)) {
        add_timeline_key_at_playhead(state, *toolbar_track);
    }
    ImGui::SameLine();
    const bool has_selected_editable_key = std::any_of(
        state->timeline_editor.selected_keys.begin(),
        state->timeline_editor.selected_keys.end(),
        [&](const TimelineKeyRef& key) {
            const TimelineTrackRow* track = find_timeline_track(tracks, key.track_id);
            return track != nullptr && timeline_track_is_editable(*track);
        });
    const bool has_remove_target = has_editable_track || has_selected_editable_key;
    if (icon_button(
            state->icons,
            Icon::RemoveKey,
            has_remove_target
                ? "Remove selected keys, or an authored key exactly at the playhead"
                : "Select an editable track (Inherit remains read-only)",
            false,
            !has_remove_target)) {
        remove_selected_timeline_keys(state, tracks);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    const bool has_key_selection = has_selected_editable_key;
    if (!has_key_selection) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Copy")) copy_selected_timeline_keys(state, tracks);
    ImGui::SameLine();
    if (ImGui::SmallButton("Cut")) cut_selected_timeline_keys(state, tracks);
    if (!has_key_selection) ImGui::EndDisabled();
    ImGui::SameLine();
    const bool paste_enabled = state->timeline_editor.clipboard.has_data &&
        state->timeline_editor.clipboard.animation_name == state->selected_animation_name;
    if (!paste_enabled) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Paste")) paste_timeline_clipboard(state, tracks);
    if (!paste_enabled) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Snap to Frames", &state->timeline_editor.snap_to_frames);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput &&
        !state->timeline_editor.retime_gesture.has_value()) {
        const bool command = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (command && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            copy_selected_timeline_keys(state, tracks);
        } else if (command && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            cut_selected_timeline_keys(state, tracks);
        } else if (command && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            paste_timeline_clipboard(state, tracks);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                   ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            remove_selected_timeline_keys(state, tracks);
        }
    }

    draw_timeline_ruler(state, duration_seconds);

    if (tracks.empty()) {
        ImGui::TextUnformatted("The selected animation does not contain keyed tracks.");
        return;
    }

    const float table_height = std::clamp(
        96.0f + static_cast<float>(tracks.size()) * 28.0f,
        180.0f,
        420.0f);
    if (ImGui::BeginTable(
            "timeline_tracks",
            2,
            ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
            ImVec2(0.0f, table_height))) {
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const TimelineTrackRow& track : tracks) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            const bool selected = timeline_track_matches_selection(*state, track);
            const Icon track_icon = track_property_icon(track.id);
            const std::string label =
                track.label + "  (" + std::to_string(track.key_times.size()) + ")";
            ImGui::PushID(track.id.c_str());
            if (icon_selectable(state->icons, track_icon, label.c_str(), selected)) {
                state->timeline_playing = false;
                focus_timeline_track(
                    state,
                    track,
                    state->timeline_time_seconds,
                    "Timeline",
                    true);
            }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            draw_timeline_lane(state, track, tracks);
        }

        ImGui::EndTable();
    }

    update_timeline_retime_gesture(state, tracks);
    draw_transform_timeline_editor(state, tracks);
}

void draw_timeline_window(
    ShellState* state,
    TimelineGraphRenderStats* graph_stats_out) {
    if (graph_stats_out != nullptr) *graph_stats_out = TimelineGraphRenderStats{};
    ImGui::Begin(kTimelineWindowTitle);
    widgets::panel_head(state->icons, Icon::NodeAnim, "Timeline");

    if (!state->load_result || !state->preview_skeleton) {
        ImGui::TextUnformatted("Load a valid project to scrub and inspect keyed animation tracks.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    if (skeleton.animations().empty()) {
        ImGui::TextUnformatted("The loaded skeleton does not define any animations.");
        ImGui::End();
        return;
    }

    const std::string combo_label =
        state->selected_animation_name.empty() ? skeleton.animations().front().name
                                               : state->selected_animation_name;
    if (ImGui::BeginCombo("Animation", combo_label.c_str())) {
        for (const auto& animation : skeleton.animations()) {
            const bool selected = state->selected_animation_name == animation.name;
            ImGui::PushID(animation.name.c_str());
            if (icon_selectable(state->icons, Icon::NodeAnim, animation.name.c_str(), selected)) {
                state->timeline_playing = false;
                set_selected_animation(state, animation.name, "Timeline", true, true);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    normalize_state_preview_settings(state);
    const auto apply_state_preview_change = [&](std::string status_message) {
        state->timeline_playing = false;
        refresh_preview_pose(state);
        state->status_message = std::move(status_message);
    };

    // Preview options collapse by default — scrubbing the clip is the
    // primary action; queueing/mix overrides are advanced.
    const bool preview_opts_open =
        widgets::section_header("Preview options", nullptr, false);
    if (preview_opts_open) {
    bool queue_enabled = state->preview_queue_enabled;
    if (ImGui::Checkbox("Queue Next Clip", &queue_enabled)) {
        state->preview_queue_enabled = queue_enabled;
        normalize_state_preview_settings(state);
        apply_state_preview_change(
            std::string(queue_enabled ? "Enabled" : "Disabled") + " queued state preview");
    }
    ImGui::SameLine();
    bool reverse_enabled = state->preview_reverse;
    if (ImGui::Checkbox("Reverse", &reverse_enabled)) {
        state->preview_reverse = reverse_enabled;
        apply_state_preview_change(
            std::string(reverse_enabled ? "Enabled" : "Disabled") + " reverse preview");
    }

    if (state->preview_queue_enabled) {
        const char* queued_label =
            state->preview_queued_animation_name.empty()
                ? "<select animation>"
                : state->preview_queued_animation_name.c_str();
        if (ImGui::BeginCombo("Queued Animation", queued_label)) {
            for (const auto& preview_animation : skeleton.animations()) {
                if (preview_animation.name == state->selected_animation_name) {
                    continue;
                }
                const bool selected =
                    state->preview_queued_animation_name == preview_animation.name;
                ImGui::PushID(preview_animation.name.c_str());
                if (icon_selectable(
                        state->icons,
                        Icon::NodeAnim,
                        preview_animation.name.c_str(),
                        selected)) {
                    state->preview_queued_animation_name = preview_animation.name;
                    apply_state_preview_change(
                        "Queued " + preview_animation.name + " after " +
                        state->selected_animation_name);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        double queue_delay = state->preview_queue_delay;
        if (ImGui::DragScalar(
                "Queue Delay",
                ImGuiDataType_Double,
                &queue_delay,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s")) {
            state->preview_queue_delay = std::max(0.0, queue_delay);
            apply_state_preview_change("Updated queued preview delay");
        }

        bool custom_mix = state->preview_use_custom_mix_duration;
        if (ImGui::Checkbox("Override Mix Duration", &custom_mix)) {
            state->preview_use_custom_mix_duration = custom_mix;
            apply_state_preview_change(
                std::string(custom_mix ? "Enabled" : "Disabled") + " custom mix preview");
        }
        if (state->preview_use_custom_mix_duration) {
            double mix_duration = state->preview_custom_mix_duration;
            if (ImGui::DragScalar(
                    "Mix Duration",
                    ImGuiDataType_Double,
                    &mix_duration,
                    0.01f,
                    nullptr,
                    nullptr,
                    "%.3f s")) {
                state->preview_custom_mix_duration = std::max(0.0, mix_duration);
                apply_state_preview_change("Updated queued preview mix duration");
            }
        }
    }
    }  // preview_opts_open

    const marrow::runtime::AnimationData* animation = selected_animation(*state);
    const double duration_seconds = timeline_preview_duration(*state);
    const std::vector<TimelineTrackRow>& tracks = cached_timeline_tracks(state);
    reconcile_timeline_key_selection(state, tracks);

    if (icon_button(
            state->icons,
            Icon::Rewind,
            "Reset to start",
            false,
            animation == nullptr)) {
        state->timeline_playing = false;
        scrub_timeline_time(state, 0.0, "Timeline", true);
    }
    ImGui::SameLine();
    if (icon_button(
            state->icons,
            Icon::PrevKey,
            "Previous keyframe",
            false,
            animation == nullptr)) {
        if (const auto previous_key = adjacent_key_time(
                tracks,
                state->timeline_time_seconds,
                false)) {
            state->timeline_playing = false;
            scrub_timeline_time(state, *previous_key, "Timeline", true);
        }
    }
    ImGui::SameLine();
    const Icon play_icon = state->timeline_playing ? Icon::Pause : Icon::Play;
    if (icon_button(
            state->icons,
            play_icon,
            state->timeline_playing ? "Pause" : "Play",
            state->timeline_playing,
            animation == nullptr || state->shell_mode == ShellMode::Parameter)) {
        if (animation != nullptr) {
            const bool was_playing = state->timeline_playing;
            state->timeline_playing = !state->timeline_playing;
            if (state->timeline_playing && !was_playing) {
                refresh_preview_pose(state);
                state->animation_state->set_listener(
                    [state](marrow::runtime::AnimationState&,
                            marrow::runtime::AnimationStateEventType type,
                            const std::shared_ptr<marrow::runtime::TrackEntry>&,
                            const marrow::runtime::AnimationEvent* event) {
                        if (type == marrow::runtime::AnimationStateEventType::Event &&
                            event != nullptr) {
                            state->preview_events.push_back(*event);
                        }
                    });
            } else if (!state->timeline_playing && was_playing) {
                if (state->animation_state) {
                    state->animation_state->set_listener({});
                }
            }
            state->status_message =
                std::string(state->timeline_playing ? "Playing " : "Paused ") +
                state->selected_animation_name;
        }
    }
    ImGui::SameLine();
    if (icon_button(
            state->icons,
            Icon::NextKey,
            "Next keyframe",
            false,
            animation == nullptr)) {
        if (const auto next_key = adjacent_key_time(
                tracks,
                state->timeline_time_seconds,
                true)) {
            state->timeline_playing = false;
            scrub_timeline_time(state, *next_key, "Timeline", true);
        }
    }
    ImGui::SameLine();
    if (icon_button(
            state->icons,
            Icon::Loop,
            "Toggle looping",
            state->timeline_loop)) {
        state->timeline_loop = !state->timeline_loop;
        refresh_preview_pose(state);
        state->status_message =
            std::string(state->timeline_loop ? "Enabled" : "Disabled") + " timeline looping";
    }

    double slider_time = state->timeline_time_seconds;
    const double minimum_time = 0.0;
    const double maximum_time = duration_seconds > 0.0 ? duration_seconds : 1.0;
    if (duration_seconds <= 0.0) {
        ImGui::BeginDisabled();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderScalar(
            "Time",
            ImGuiDataType_Double,
            &slider_time,
            &minimum_time,
            &maximum_time,
            "%.3fs")) {
        state->timeline_playing = false;
        scrub_timeline_time(state, slider_time, "Timeline", true);
    }
    if (duration_seconds <= 0.0) {
        ImGui::EndDisabled();
    }

    ImGui::Text(
        "Preview span: %s   Keyed tracks: %zu   Root motion total: (%.2f, %.2f)",
        format_time_seconds(duration_seconds).c_str(),
        tracks.size(),
        state->preview_root_motion_total.x,
        state->preview_root_motion_total.y);

    if (ImGui::BeginTabBar("timeline_views")) {
        if (ImGui::BeginTabItem(
                "Dopesheet",
                nullptr,
                state->timeline_editor.requested_view_mode == TimelineViewMode::Dopesheet
                    ? ImGuiTabItemFlags_SetSelected
                    : ImGuiTabItemFlags_None)) {
            state->timeline_editor.view_mode = TimelineViewMode::Dopesheet;
            draw_dopesheet_body(state, tracks, duration_seconds);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(
                "Graph",
                nullptr,
                state->timeline_editor.requested_view_mode == TimelineViewMode::Graph
                    ? ImGuiTabItemFlags_SetSelected
                    : ImGuiTabItemFlags_None)) {
            state->timeline_editor.view_mode = TimelineViewMode::Graph;
            const auto stats = draw_timeline_graph_body(state, tracks);
            if (graph_stats_out != nullptr) *graph_stats_out = stats;
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        state->timeline_editor.requested_view_mode.reset();
    }

    ImGui::End();
}


} // namespace marrow::editor::shell
