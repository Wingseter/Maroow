#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
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
 * @brief Authors one explicit animation duration through the ordered edit log.
 *
 * The requested value is normalized to runtime float32 precision and must not
 * be shorter than the effective animation's inferred duration. Rejected edits
 * leave the project unchanged.
 */
AuthoringResult set_animation_duration(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    double duration);

/**
 * @brief Grows explicit durations to cover all current typed timeline overlays.
 *
 * Animations without an authored explicit duration remain inference-driven.
 * This operation never shrinks a duration and applies all required growth
 * atomically.
 */
AuthoringResult auto_extend_explicit_animation_durations(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton);

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

/** @brief One editable scalar channel of a persisted timeline key. */
enum class TimelineScalarComponent : std::uint8_t {
    Angle,
    X,
    Y,
    Red,
    Green,
    Blue,
    Alpha,
};

struct TimelineScalarOffsetResult : AuthoringResult {
    double applied_delta{0.0};
    std::size_t key_count{0U};
};

/**
 * @brief Atomically offsets one scalar component of persisted timeline keys.
 *
 * Every selector must resolve to a Transform or Slot Color key whose family
 * supports `component`. Slot Color deltas are clamped group-wide so no key
 * leaves [0, 1] and no imported out-of-range key is pushed further out;
 * Angle, X, and Y are unclamped. Rotate angles are setup-relative in the
 * project and absolute in the graph, but a delta is identical in both spaces,
 * so no setup-pose conversion occurs. Times and interpolations are never
 * written. A rejected edit leaves the project unchanged.
 */
TimelineScalarOffsetResult offset_keyframe_scalars(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineScalarComponent component,
    double requested_delta);

struct TimelineInterpolationResult : AuthoringResult {
    std::size_t key_count{0U};
    std::size_t changed_key_count{0U};
};

/**
 * @brief Atomically replaces the outgoing easing of persisted timeline keys.
 *
 * The easing is a property of the whole parent key and is shared by every
 * component of that key, so this operation takes no component argument. Cubic
 * control points must be finite, must survive float32 narrowing, and must keep
 * `cx1`/`cx2` inside [0, 1], which is the same invariant the `.marrow` and
 * `.mskl` loaders enforce and exactly the condition that makes the runtime's
 * `X(t) = alpha` inverse well posed. Finite Y overshoot is allowed. Linear and
 * Stepped ignore `control_points` entirely, because the stored value carries
 * none. Draw-order, event, and slot-attachment keys carry no easing and are
 * rejected. Callers materialize imported runtime-only tracks through the
 * shared `ensure_*_timeline_edit` project operations first. A rejected edit
 * leaves the project unchanged.
 */
TimelineInterpolationResult set_keyframe_interpolation(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    runtime::InterpolationKind kind,
    const std::array<double, 4>& control_points = {0.0, 0.0, 1.0, 1.0});

} // namespace marrow::editor
