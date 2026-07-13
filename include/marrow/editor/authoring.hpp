#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/project.hpp"

namespace marrow::editor {

struct AuthoringResult {
    bool changed{false};
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

enum class TimelineKeyKind {
    Transform,
    Deform,
    DrawOrder,
    Event,
    SlotColor,
    SlotAttachment,
};

/**
 * @brief Stable project-domain selector for one persisted timeline key.
 *
 * Fields not used by the selected kind remain empty. Event keys use
 * `same_time_ordinal` to distinguish stable same-time entries.
 */
struct TimelineKeySelector {
    TimelineKeyKind kind{TimelineKeyKind::Transform};
    std::string animation_name;
    std::string bone_name;
    TransformTimelineChannel transform_channel{TransformTimelineChannel::Rotate};
    std::string slot_name;
    std::string attachment_name;
    double time{0.0};
    std::size_t same_time_ordinal{0U};
};

struct TimelineRetimeResult : AuthoringResult {
    double applied_delta{0.0};
    std::size_t key_count{0U};
};

AuthoringResult create_animation(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    std::string_view animation_name);

AuthoringResult duplicate_animation(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    std::string_view source_animation,
    std::string_view animation_name);

AuthoringResult rename_animation(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    std::string_view source_animation,
    std::string_view animation_name);

AuthoringResult delete_animation(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    std::string_view animation_name);

/**
 * @brief Atomically retimes persisted keys by one shared delta.
 *
 * The requested delta is optionally frame-snapped, then clamped against zero
 * and unselected neighbors. Non-event tracks retain a 1 ms separation while
 * event ties remain stable. Callers materialize imported runtime-only tracks
 * through the shared `ensure_*_timeline_edit` project operations first.
 */
TimelineRetimeResult retime_keyframes(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    double requested_delta,
    bool snap_to_frames,
    double frames_per_second);

} // namespace marrow::editor
