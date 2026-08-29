#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/preferences.hpp"
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
 * be shorter than the effective animation's inferred duration. A managed loop
 * boundary key does not constrain the duration it follows, so an opted-in clip
 * stays shortenable; every animation with no opted-in lane validates exactly as
 * before. Rejected edits leave the project unchanged.
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

// `TimelineScalarComponent` now lives in `marrow/editor/project.hpp`, because
// `TransformKeyframeEdit` and `SlotColorKeyframeEdit` store one as a curve
// driver. This header includes that one, so every existing include site is
// unaffected.

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
 *
 * MAR-171 side effect: **every key this writes becomes
 * `TimelineCurveMode::Manual`**, and `changed_key_count` counts a key whose
 * mode changed even when its four control points did not. Writing an absolute
 * easing is exactly the act of taking a key off its neighbours, so the rule
 * lives here rather than at the four call sites that could each forget it.
 */
TimelineInterpolationResult set_keyframe_interpolation(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    runtime::InterpolationKind kind,
    const std::array<double, 4>& control_points = {0.0, 0.0, 1.0, 1.0});


/**
 * @brief One fixed, deterministic easing preset.
 *
 * `control_points` is unused for Linear and Stepped, whose stored easing
 * carries none. The four cubic quadruples are the CSS Easing Level 1 timing
 * functions, reproduced exactly.
 */
struct CurvePresetDefinition {
    CurvePreset preset{CurvePreset::Linear};
    std::string_view token;         // the editor-settings.json token
    std::string_view display_name;  // "Ease-In-Out"
    runtime::InterpolationKind kind{runtime::InterpolationKind::Linear};
    std::array<double, 4> control_points{};
};

/**
 * @brief The six fixed presets in stable enum, UI, and identity-search order.
 *
 * This is the only place the preset numbers exist. A file-local
 * `static_assert` in `authoring.cpp` proves at compile time that the table is
 * in enum order and that every cubic entry keeps `cx1`/`cx2` inside [0, 1] —
 * the invariant the `.marrow` and `.mskl` loaders enforce.
 */
inline constexpr std::array<CurvePresetDefinition, 6> kCurvePresets{{
    {CurvePreset::Linear, "linear", "Linear",
     runtime::InterpolationKind::Linear, {0.0, 0.0, 0.0, 0.0}},
    {CurvePreset::Stepped, "stepped", "Stepped",
     runtime::InterpolationKind::Stepped, {0.0, 0.0, 0.0, 0.0}},
    {CurvePreset::Ease, "ease", "Ease",
     runtime::InterpolationKind::CubicBezier, {0.25, 0.1, 0.25, 1.0}},
    {CurvePreset::EaseIn, "ease_in", "Ease-In",
     runtime::InterpolationKind::CubicBezier, {0.42, 0.0, 1.0, 1.0}},
    {CurvePreset::EaseOut, "ease_out", "Ease-Out",
     runtime::InterpolationKind::CubicBezier, {0.0, 0.0, 0.58, 1.0}},
    {CurvePreset::EaseInOut, "ease_in_out", "Ease-In-Out",
     runtime::InterpolationKind::CubicBezier, {0.42, 0.0, 0.58, 1.0}},
}};

/** @brief The definition of one preset; total over the closed enum. */
const CurvePresetDefinition& curve_preset_definition(CurvePreset preset);

/** @brief The runtime easing one preset denotes. */
runtime::Interpolation curve_preset_interpolation(CurvePreset preset);

/**
 * @brief Names the preset an easing exactly equals, or nullopt for a custom curve.
 *
 * Cubic control points are compared bit-exactly after narrowing the table's
 * doubles to `runtime::AnimationScalar`, so a preset written by this editor
 * always reads back as that preset, including after save, reload, and export.
 * There is no epsilon: an approximate match would name a hand-dragged curve
 * "Ease" when a save/reload would show different numbers.
 */
std::optional<CurvePreset> curve_preset_of(const runtime::Interpolation& interpolation);

/** @brief Parses one preset token; the same six tokens `editor-settings.json` uses. */
std::optional<CurvePreset> curve_preset_from_token(std::string_view token);

/** @brief The `.marrow` token for one curve mode, and its inverse. */
std::string_view curve_mode_token(TimelineCurveMode mode);
std::optional<TimelineCurveMode> curve_mode_from_token(std::string_view token);
/** @brief The `.marrow` token for one driver component, and its inverse. */
std::string_view curve_driver_token(TimelineScalarComponent driver);
std::optional<TimelineScalarComponent> curve_driver_from_token(std::string_view token);
/** @brief The lowest-indexed component a family owns, used as its default driver. */
TimelineScalarComponent default_curve_driver(
    TimelineKeyKind kind,
    TransformTimelineChannel channel);
/** @brief Reports whether `driver` is authorable on that family. */
bool curve_driver_is_authorable(
    TimelineKeyKind kind,
    TransformTimelineChannel channel,
    TimelineScalarComponent driver);

struct TimelineCurveModeResult : AuthoringResult {
    std::size_t key_count{0U};
    std::size_t changed_key_count{0U};   // mode or driver differed
    std::size_t resolved_key_count{0U};  // stored easing rewritten
};

/**
 * @brief Atomically records manual/automatic curve intent on persisted keys.
 *
 * Only Transform and Slot Color keys carry curve intent: a deform key's value
 * is a vertex-offset vector with no canonical scalar to drive a tangent, and
 * the discrete families carry no easing at all. `driver` must name a component
 * the selected key's family owns; `std::nullopt` selects that family's
 * lowest-indexed component. `Manual` records intent only and never writes a
 * driver, so there is exactly one representation of a manual key. Setting
 * `Auto` immediately resolves every affected automatic curve of every animation
 * the selectors name, so the stored easing and the recorded intent never
 * disagree after a successful call. A rejected edit leaves the project
 * unchanged.
 */
TimelineCurveModeResult set_keyframe_curve_mode(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineCurveMode mode,
    std::optional<TimelineScalarComponent> driver = std::nullopt);

struct TimelineAutoCurveResult : AuthoringResult {
    std::size_t auto_key_count{0U};      // auto keys with an outgoing segment
    std::size_t resolved_key_count{0U};  // keys whose stored curve changed
};

/**
 * @brief Recomputes every automatic curve of one animation, or of the project.
 *
 * Callers run this inside the transaction that changed a key time, a key value,
 * a key's existence, or an explicit duration, so one edit stays one history
 * entry. Tracks with no automatic key are skipped untouched. A track whose
 * driver series is non-finite, or which contains a segment shorter than the key
 * time epsilon, rejects the whole call atomically rather than resolving part of
 * it. This never demotes a key and never writes a manual key.
 */
TimelineAutoCurveResult resolve_automatic_curves(
    ProjectData* project,
    std::string_view animation_name = {});

/** @brief Which timeline family a lane selector names. */
enum class TimelineLaneKind : std::uint8_t { Transform, SlotColor, Deform };

/**
 * @brief Stable project-domain selector for one persisted timeline lane.
 *
 * Unlike `TimelineKeySelector`, this names a whole timeline and carries no
 * time, because loop synchronization is a lane-level property whose identity
 * no retime, insertion, deletion, or paste can change. Fields not used by the
 * selected kind remain empty.
 */
struct TimelineLaneSelector {
    TimelineLaneKind kind{TimelineLaneKind::Transform};
    std::string animation_name;
    std::string bone_name;
    TransformTimelineChannel transform_channel{TransformTimelineChannel::Rotate};
    std::string slot_name;
    std::string attachment_name;
};

/** @brief What the contract did to one lane's boundary key. */
enum class TimelineLoopBoundaryAction : std::uint8_t {
    Unchanged,
    Created,
    Adopted,
    Moved,
    Rewritten,
    Released,
};

struct TimelineLoopSyncResult : AuthoringResult {
    std::size_t lane_count{0U};               // opted-in lanes in scope
    std::size_t changed_lane_count{0U};       // the flag differed
    std::size_t synchronized_lane_count{0U};  // the boundary key differed
    std::size_t created_key_count{0U};
    std::size_t moved_key_count{0U};
    std::size_t rewritten_key_count{0U};
    std::size_t resolved_key_count{0U};       // from the MAR-171 resolver pass
    std::vector<TimelineLoopBoundaryAction> lane_actions;  // parallel to selectors
};

/**
 * @brief Atomically records loop-boundary synchronization intent on lanes.
 *
 * Only Transform, Slot Color, and Deform timelines can be synchronized: the
 * discrete families are piecewise constant, already wrap without a pop, and an
 * event key at the boundary would fire twice per loop. Enabling requires the
 * lane's animation to carry an explicit duration of at least one millisecond
 * and the lane to hold a key exactly at time zero, and immediately creates or
 * adopts one managed key at that duration mirroring the time-zero key.
 * Disabling evaluates no prerequisite and leaves the managed key in place as an
 * ordinary key, so a project that has reached an unsatisfiable state always has
 * an escape. A rejected edit leaves the project unchanged.
 */
TimelineLoopSyncResult set_timeline_loop_sync(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    const std::vector<TimelineLaneSelector>& lanes,
    bool enabled);

/**
 * @brief Re-establishes the loop-boundary contract on every opted-in lane.
 *
 * Callers run this inside the transaction that changed a key value, a key time,
 * a key's existence, an automatic curve, or an explicit duration, so one edit
 * stays one history entry. Projects with no opted-in lane return immediately
 * having done nothing at all, including no automatic-curve resolution. A lane
 * that cannot satisfy its contract rejects the whole call atomically rather
 * than synchronizing part of it, naming the animation, the timeline, and the
 * remedy.
 */
TimelineLoopSyncResult synchronize_loop_boundaries(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name = {});

/**
 * @brief The animation's inferred duration with managed loop boundaries excluded.
 *
 * A loop-synchronized lane's last key is placed at the explicit duration by the
 * boundary contract, so it must not act as a floor on the duration it follows.
 * Every opted-in lane is materialized in `project`, so this walks the effective
 * animation, skips every timeline an opted-in project lane owns, and folds
 * those lanes back in at their second-to-last key time.
 *
 * Returns `animation.inferred_duration()` unchanged, bit for bit, when no lane
 * of `animation` is opted in, so every existing project keeps byte-identical
 * duration validation.
 */
double inferred_duration_excluding_loop_boundaries(
    const ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    const runtime::AnimationData& animation);

/**
 * @brief Reports whether one persisted key is a lane's managed loop boundary.
 *
 * Identity is derived, never stored. The contract owns a lane's last key when
 * that key satisfies one half of the contract: it already sits at
 * `float32(explicit duration)`, or it is still the bit-exact mirror of the key
 * at time zero that a previous synchronization wrote, which is what makes a
 * duration change a move rather than a promotion. Every other key of an
 * opted-in lane, and every key of a lane that is not opted in, is authored data.
 *
 * The GUI skips such a key and reports it; the Agent rejects it, naming the
 * remedy. Both surfaces call this so they can never disagree about which key is
 * derived, and `synchronize_loop_boundaries()` uses the same derivation, so a
 * key this reports as authored is a key the sync will never overwrite.
 */
bool timeline_key_is_managed_loop_boundary(
    const ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    const TimelineKeySelector& selector);

/** @brief The `.marrow` token for one lane kind, and its inverse. */
std::string_view timeline_lane_kind_token(TimelineLaneKind kind);
std::optional<TimelineLaneKind> timeline_lane_kind_from_token(std::string_view token);

/** @brief Which edge of the selection's time range stays fixed while scaling. */
enum class TimelineScalePivot : std::uint8_t {
    RangeStart,  // the earliest selected time is the pivot; the late edge moves
    RangeEnd,    // the latest selected time is the pivot; the early edge moves
};

struct TimelineScaleResult : AuthoringResult {
    double pivot_time{0.0};
    double applied_scale{1.0};
    double original_span{0.0};
    double scaled_span{0.0};
    std::size_t key_count{0U};
    std::size_t moved_key_count{0U};
    /**
     * @brief The time each selector actually resolved to, in selector order.
     *
     * A selector's own `time` only has to identify a key within the resolver's
     * one-microsecond window, and a shell selector carries it narrowed to
     * `float32`, so it is an identity rather than a value. Reporting surfaces
     * that echo the request instead of reading this are reporting what the
     * caller asked for, not what the project holds. Empty on a rejection.
     */
    std::vector<double> previous_times;
};

/**
 * @brief Why a selection cannot be scaled at all, or empty when it can.
 *
 * Reports only the **selection-shaped** refusals a caller can fix before
 * dragging — a key pinned by loop synchronization, and a selection naming part
 * of an event tie. It deliberately reports no collision, because a collision
 * depends on the ratio and is decided per frame by `scale_keyframe_times()`,
 * which stays the sole authority on whether one call is legal.
 *
 * The GUI calls this to refuse to arm a drag with a message, rather than
 * letting the first frame fail; both answers come from the same private
 * predicates the primitive uses, so the two surfaces cannot disagree about
 * which key is pinned or which tie is split.
 */
std::string timeline_scale_selection_refusal(
    const ProjectData& project,
    const std::vector<TimelineKeySelector>& selectors);

/**
 * @brief Atomically scales persisted key times about one edge of their range.
 *
 * The pivot is never a caller-supplied time: it is the opposite edge of the
 * resolved selectors' own time range, so `t' = pivot + (t - pivot) * scale`
 * leaves the pivot key bit-identical by construction. `scale` must be finite
 * and strictly positive; a ratio of exactly one, or one that moves nothing,
 * reports `changed == false` with no error and writes nothing.
 *
 * Unlike `retime_keyframes()`, this **rejects** rather than clamps. Any
 * projected pair on an affected timeline that would fall closer than the
 * family's minimum separation — including a selected key intruding on an
 * unselected neighbour — rejects the whole call. Event keys sharing a time are
 * carried together because the mapping is a function of time, and a selection
 * naming only part of such a tie is rejected by name. A key pinned by loop
 * synchronization rejects rather than pinning, because a partially pinned scale
 * is not a scale.
 *
 * Only `keyframe.time` is written. Callers materialize imported runtime-only
 * tracks through the shared `ensure_*_timeline_edit` project operations first,
 * and re-resolve automatic curves afterwards inside the same transaction. A
 * rejected edit leaves the project unchanged.
 */
TimelineScaleResult scale_keyframe_times(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineScalePivot pivot,
    double scale);

} // namespace marrow::editor
