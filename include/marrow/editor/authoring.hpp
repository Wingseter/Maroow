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
    std::vector<std::string> dependencies;

    explicit operator bool() const noexcept { return error.empty(); }
};

/** @brief Creates the optional parameter-model container on first mutation. */
ParameterModel& ensure_parameter_model(ProjectData* project);

AuthoringResult create_parameter(
    ProjectData* project,
    ParameterAuthoringDefinition definition);
AuthoringResult update_parameter(
    ProjectData* project,
    std::string_view parameter_id,
    ParameterAuthoringDefinition definition);
AuthoringResult delete_parameter(ProjectData* project, std::string_view parameter_id);

AuthoringResult create_parameter_group(
    ProjectData* project,
    ParameterGroupAuthoringDefinition definition);
AuthoringResult update_parameter_group(
    ProjectData* project,
    std::string_view group_id,
    ParameterGroupAuthoringDefinition definition);
AuthoringResult delete_parameter_group(ProjectData* project, std::string_view group_id);

/**
 * @brief Creates or replaces one ID-stable raw parameter-shape definition.
 * @param replace_existing Must be true when the ID already exists.
 */
AuthoringResult upsert_parameter_shape(
    ProjectData* project,
    runtime::json::Value definition,
    bool replace_existing = false);
AuthoringResult delete_parameter_shape(ProjectData* project, std::string_view shape_id);

AuthoringResult upsert_parameter_deformer(
    ProjectData* project,
    runtime::json::Value definition,
    bool replace_existing = false);
AuthoringResult delete_parameter_deformer(ProjectData* project, std::string_view deformer_id);

AuthoringResult upsert_expression(
    ProjectData* project,
    runtime::json::Value definition,
    bool replace_existing = false);
AuthoringResult delete_expression(ProjectData* project, std::string_view expression_id);

/** @brief Upserts one lip-sync mapping by its target `parameter` ID. */
AuthoringResult upsert_lip_sync_mapping(
    ProjectData* project,
    runtime::json::Value mapping);
AuthoringResult delete_lip_sync_mapping(ProjectData* project, std::string_view parameter_id);

/**
 * @brief Inserts one captured shape/deformer keyform at its authored coordinates.
 *
 * The caller supplies the UI-free captured payload. For blend shapes this must
 * contain animation-FFD-only offsets; warp and rotation callers supply the
 * currently evaluated lattice or angle. Existing coordinates are replaced
 * only when `replace_existing` is true.
 */
AuthoringResult capture_deformer_keyform(
    ProjectData* project,
    std::string_view deformer_id,
    runtime::json::Value keyform,
    bool replace_existing = false);

/**
 * @brief Captures the current UI-independent preview evaluation as one keyform.
 *
 * Shapes capture animation FFD only. Warp and rotation definitions evaluate
 * their current local lattice/angle from the runtime's final parameter values.
 */
AuthoringResult capture_current_deformer_keyform(
    ProjectData* project,
    const runtime::SkeletonData& runtime_data,
    const runtime::Skeleton& preview_skeleton,
    std::string_view deformer_id,
    bool replace_existing = false);

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
