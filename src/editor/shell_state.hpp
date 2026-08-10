#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "imgui.h"
#include "sokol_gfx.h"

#include "icon_registry.hpp"
#include "shell_asset_watch.hpp"
#include "viewport_renderer.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/editor/agent_control.hpp"
#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/editor/selection.hpp"
#include "marrow/editor/session.hpp"
#include "session_shell_binding.hpp"
#include "windowing.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/profiler.hpp"

namespace marrow::editor::shell {

class AgentSocketServer;

struct Options {
    std::filesystem::path project_path{"assets/fixtures/player_idle.marrow"};
    std::optional<int> auto_close_frames;
    bool verify_launch_focus{false};
    std::optional<int> agent_port;
    std::string agent_token;
};

enum class ParseStatus {
    Ok,
    Help,
    Error,
};

struct ParseResult {
    ParseStatus status{ParseStatus::Error};
    Options options;
};

struct BoneCanvasNode {
    std::size_t bone_index{0};
    std::optional<std::size_t> parent_index;
    ImVec2 screen_position{};
    bool active{true};
};

struct ViewportCamera {
    bool initialized{false};
    double world_center_x{0.0};
    double world_center_y{0.0};
    double fit_extent_x{80.0};
    double fit_extent_y{80.0};
};

struct ViewportWorldPoint {
    double x{0.0};
    double y{0.0};
};

struct ViewportLayout {
    ImVec2 canvas_origin{};
    ImVec2 canvas_size{};
    ImVec2 canvas_end{};
    ImVec2 screen_center{};
    ImVec2 world_origin_screen{};
    double world_center_x{0.0};
    double world_center_y{0.0};
    float pixels_per_unit{1.0f};
    float render_joint_radius{6.0f};
    std::vector<BoneCanvasNode> bones;
};

// Narrow, screen-space targets win over broader rendered surfaces. The
// viewport UI consumes an active edit/brush and the transform gizmos before
// consulting this entity order. Within one category, the nearest screen-space
// candidate wins, followed by stable authored order (or reverse draw order for
// overlapping rendered attachments).
enum class ViewportEntityHitPriority : std::uint8_t {
    ConstraintTarget,
    BoneJoint,
    BoneBody,
    SlotHandle,
    AttachmentSurface,
};

struct ViewportEntityHitCandidate {
    marrow::editor::SelectionItem item;
    ViewportEntityHitPriority priority{ViewportEntityHitPriority::AttachmentSurface};
    float distance_squared{0.0f};
    std::size_t stable_order{0U};
};

enum class ViewportHitMarkerShape : std::uint8_t {
    Circle,
    Diamond,
};

struct ViewportHitCircle {
    ViewportEntityHitCandidate candidate;
    ImVec2 center{};
    float radius{0.0f};
    ViewportHitMarkerShape marker_shape{ViewportHitMarkerShape::Circle};
};

struct ViewportHitSegment {
    ViewportEntityHitCandidate candidate;
    ImVec2 start{};
    ImVec2 end{};
    float radius{0.0f};
};

struct ViewportHitTriangle {
    ViewportEntityHitCandidate candidate;
    std::array<ImVec2, 3> points{};
};

struct ViewportEntityHitGeometry {
    std::vector<ViewportHitCircle> circles;
    std::vector<ViewportHitSegment> segments;
    std::vector<ViewportHitTriangle> triangles;
};

struct OnionSkinGhostPose {
    std::vector<BoneCanvasNode> bones;
    double time_seconds{0.0};
    int distance_rank{0};
    bool before_current{true};
    ImU32 line_color{0};
    ImU32 fill_color{0};
    ImU32 outline_color{0};
};

struct OnionSkinSampleSpec {
    double time_seconds{0.0};
    int distance_rank{0};
    bool before_current{true};
};

struct OnionSkinTexturedGhost {
    marrow::renderer::PreparedScene scene;
    std::array<float, 4> tint_color{};
};

struct ViewportRenderVertex {
    float position_x{0.0f};
    float position_y{0.0f};
    float color_r{0.0f};
    float color_g{0.0f};
    float color_b{0.0f};
    float color_a{0.0f};
};

struct ViewportGeometryPass {
    std::vector<ViewportRenderVertex> line_vertices;
    std::vector<ViewportRenderVertex> triangle_vertices;
};

struct ViewportFramebufferSize {
    int width{0};
    int height{0};
};

struct ViewportRenderResources {
    bool available{false};
    bool initialization_attempted{false};
    sg_image color_image{};
    sg_view color_attachment_view{};
    sg_view color_texture_view{};
    sg_image depth_stencil_image{};
    sg_view depth_stencil_view{};
    sg_sampler texture_sampler{};
    std::uint64_t imgui_texture_id{0U};
    sg_shader overlay_shader{};
    sg_pipeline overlay_line_pipeline{};
    sg_pipeline overlay_triangle_pipeline{};
    sg_buffer overlay_vertex_buffer{};
    std::size_t overlay_vertex_capacity_bytes{0U};
    marrow::editor::ViewportRenderer prepared_scene_renderer{};
    int framebuffer_width{0};
    int framebuffer_height{0};
    std::string error_message;
};

// Bump when the default split changes so a fresh rebuild is forced.
constexpr int kDockLayoutVersion = 4;

struct DockLayoutState {
    ImGuiID dockspace_id{0};
    ImGuiID viewport_node_id{0};
    ImGuiID timeline_node_id{0};
    ImGuiID hierarchy_node_id{0};
    ImGuiID properties_node_id{0};
    ImGuiID agent_node_id{0};
    int layout_version{0};
};

struct PreviewAttachmentSelection {
    std::size_t slot_index{0};
    std::optional<std::size_t> skin_index;
    std::string attachment_name;
};

struct SlotAttachmentReference {
    std::size_t slot_index{0};
    std::optional<std::size_t> skin_index;
    const marrow::runtime::AttachmentData* attachment{nullptr};
};

struct MeshWeightInfluenceRow {
    std::string bone_name;
    double bind_x{0.0};
    double bind_y{0.0};
    double weight{0.0};
};

struct MeshWeightVertexRow {
    std::size_t vertex_index{0};
    double local_x{0.0};
    double local_y{0.0};
    std::vector<MeshWeightInfluenceRow> influences;
};

enum class WeightPaintMode {
    Paint,
    Erase,
    Smooth,
};

enum class ShellMode {
    Setup = 0,
    Animation = 1,
    WeightPaint = 2,
    Parameter = 3,
};

struct WeightPaintSettings {
    bool enabled{false};
    WeightPaintMode mode{WeightPaintMode::Paint};
    float radius_pixels{44.0f};
    float strength{0.35f};
    bool show_heatmap{true};
};

struct MeshWeightPaintTarget {
    std::size_t slot_index{0};
    std::optional<std::size_t> source_skin_index;
    std::string source_skin_name;
    std::string slot_name;
    std::string source_attachment_name;
    std::string display_attachment_name;
    const marrow::runtime::AttachmentData* source_attachment{nullptr};
    const marrow::runtime::AttachmentData* display_attachment{nullptr};
};

struct MeshWeightOverlayVertex {
    ImVec2 screen_position{};
    marrow::runtime::MeshWorldVertex world_position{};
    double weight{0.0};
};

struct MeshWeightOverlay {
    MeshWeightPaintTarget target;
    std::vector<MeshWeightOverlayVertex> vertices;
    std::vector<std::size_t> triangles;
    std::vector<std::vector<std::size_t>> neighbors;
    std::vector<double> vertex_offsets;
};

struct DebugOverlayLineSegment {
    ImVec2 start{};
    ImVec2 end{};
    ImU32 color{0};
    float thickness{1.0f};
};

struct DebugOverlayCircle {
    ImVec2 center{};
    float radius{0.0f};
    ImU32 fill_color{0};
    ImU32 outline_color{0};
    float outline_thickness{1.0f};
};

struct DebugOverlayStats {
    bool bones_enabled{false};
    std::size_t ik_constraint_count{0};
    std::size_t path_constraint_count{0};
    std::size_t physics_constraint_count{0};
    std::size_t mesh_attachment_count{0};
    std::size_t bounding_box_count{0};
};

struct DebugOverlayGeometry {
    std::vector<DebugOverlayLineSegment> lines;
    std::vector<DebugOverlayCircle> circles;
    DebugOverlayStats stats{};
};

struct TimelineTrackRow {
    std::string id;
    std::string label;
    std::string animation_name;
    std::vector<double> key_times;
    std::optional<std::size_t> bone_index;
    std::optional<std::size_t> slot_index;
    std::optional<marrow::editor::TransformTimelineChannel> transform_channel;
    std::optional<std::string> deform_attachment_name;
};

struct TimelineTrackCache {
    std::uint64_t runtime_revision{0U};
    const marrow::runtime::SkeletonData* skeleton_identity{nullptr};
    std::string animation_name;
    std::vector<TimelineTrackRow> tracks;
    std::uint64_t generation{0U};
    bool valid{false};
};

struct SlotDerivedCacheEntry {
    std::vector<SlotAttachmentReference> authored_attachments;
    std::vector<std::string> timeline_attachment_names;
};

struct SlotDerivedCache {
    std::uint64_t runtime_revision{0U};
    std::shared_ptr<const marrow::runtime::SkeletonData> runtime;
    std::vector<SlotDerivedCacheEntry> slots;
    std::uint64_t generation{0U};
    bool valid{false};
};

struct ShellState;

struct EditorHistorySnapshot {
    marrow::editor::ProjectData project;
    std::string serialized_project;
    marrow::editor::PreviewState preview_state;
    std::vector<std::string> preview_skin_names;
    std::vector<std::optional<PreviewAttachmentSelection>> preview_slot_overrides;
    std::uint64_t runtime_revision{0U};
};

struct MeshWeightStrokeState {
    bool active{false};
    bool changed{false};
    EditorHistorySnapshot before_snapshot;
    std::string label;
    std::string group;
    ImVec2 last_sample_position{};
    bool has_last_sample{false};
};

enum class EditActionKind {
    MoveBone,
    AddKeyframe,
    RemoveKeyframe,
    EditProperty,
};

bool apply_history_snapshot(ShellState* state, const EditorHistorySnapshot& snapshot);

struct PendingEditAction {
    ImGuiID item_id{0};
    EditActionKind kind{EditActionKind::EditProperty};
    std::string label;
    std::string group;
    bool allow_merge{false};
    EditorHistorySnapshot before_snapshot;
};

struct AnimationDurationGesture {
    std::string animation_name;
    bool changed{false};
    marrow::editor::EditorSession::EditTransaction transaction;
};

struct InspectorTransformGesture {
    ImGuiID item_id{0};
    marrow::editor::TransformTimelineChannel channel{
        marrow::editor::TransformTimelineChannel::Rotate};
    bool changed{false};
    marrow::editor::EditorSession::EditTransaction transaction;
};

enum class ViewportTranslateAxis {
    Free,
    X,
    Y,
};

struct ViewportTranslateGesturePayload {
    ViewportTranslateAxis axis{ViewportTranslateAxis::Free};
    ViewportWorldPoint pointer_start{};
    ViewportWorldPoint bone_world_start{};
};

struct ViewportRotationBasis {
    ViewportWorldPoint pivot_world{};
    double inverse_a{1.0};
    double inverse_b{0.0};
    double inverse_c{0.0};
    double inverse_d{1.0};
    marrow::runtime::BoneInherit inherit{marrow::runtime::BoneInherit::Normal};
};

struct ViewportRotateGesturePayload {
    ViewportRotationBasis basis{};
    double start_absolute_rotation{0.0};
    std::optional<double> previous_wrapped_angle;
    double accumulated_rotation{0.0};
    bool angular_reference_suspended{false};
    ImVec2 pointer_screen{};
    double current_absolute_rotation{0.0};
};

enum class ViewportScaleHandle {
    X,
    Y,
    Uniform,
};

struct ViewportScaleBasis {
    ViewportWorldPoint pivot_world{};
    ImVec2 positive_x_screen_direction{1.0f, 0.0f};
    ImVec2 positive_y_screen_direction{0.0f, -1.0f};
    ImVec2 uniform_screen_direction{
        0.7071067811865475f,
        -0.7071067811865475f};
    marrow::runtime::BoneInherit inherit{marrow::runtime::BoneInherit::Normal};
};

struct ViewportScaleCandidate {
    double scale_x{1.0};
    double scale_y{1.0};
};

struct ViewportScaleGesturePayload {
    ViewportScaleBasis basis{};
    ViewportScaleHandle handle{ViewportScaleHandle::X};
    double start_absolute_scale_x{1.0};
    double start_absolute_scale_y{1.0};
    double start_projection_pixels{0.0};
    ImVec2 pointer_screen{};
    double current_absolute_scale_x{1.0};
    double current_absolute_scale_y{1.0};
};

using ViewportTransformGesturePayload = std::variant<
    ViewportTranslateGesturePayload,
    ViewportRotateGesturePayload,
    ViewportScaleGesturePayload>;

struct ViewportTransformGesture {
    std::size_t bone_index{0U};
    std::string bone_name;
    std::string animation_name;
    double time_seconds{0.0};
    marrow::editor::SelectionSet selection_before;
    std::optional<marrow::editor::SelectionItem> hierarchy_anchor_before;
    std::optional<std::string> timeline_focus_before;
    bool changed{false};
    marrow::editor::EditorSession::EditTransaction transaction;
    ViewportTransformGesturePayload payload{};
};

struct ViewportBoxSelectionGesture {
    ImVec2 start{};
    ImVec2 current{};
    bool additive{false};
    bool dragged{false};
};

struct TimelineKeyRef {
    std::string track_id;
    // Indices are presentation details and shift whenever a key is inserted or
    // removed. Keep a quantized time identity instead, plus enough same-time
    // context to distinguish event keys and invalidate ambiguous identities
    // when the same-time population changes.
    std::int64_t time_microseconds{0};
    std::size_t same_time_ordinal{0U};
    std::size_t same_time_count{1U};

    friend bool operator==(const TimelineKeyRef& left, const TimelineKeyRef& right) {
        return left.track_id == right.track_id &&
            left.time_microseconds == right.time_microseconds &&
            left.same_time_ordinal == right.same_time_ordinal &&
            left.same_time_count == right.same_time_count;
    }
};

struct TimelineClipboard {
    bool has_data{false};
    std::string animation_name;
    double earliest_time{0.0};
    // A typed project fragment keeps every supported key payload intact while
    // remaining independent from the operating-system text clipboard.
    marrow::editor::ProjectData project_fragment;
};

struct TimelineBoxSelection {
    std::string track_id;
    double start_time{0.0};
    double current_time{0.0};
    bool additive{false};
};

struct TimelineRetimeGesture {
    ImGuiID item_id{0};
    float start_mouse_x{0.0f};
    std::vector<TimelineKeyRef> keys;
    std::vector<double> original_times;
    double applied_delta{0.0};
    bool materialized{false};
    bool changed{false};
    marrow::editor::EditorSession::EditTransaction transaction;
};

struct ParameterSliderGesture {
    std::string parameter_id;
    bool changed{false};
    marrow::editor::EditorSession::EditTransaction transaction;
};

struct ParameterGeometryGesture {
    std::string deformer_id;
    std::string field;
    bool changed{false};
    marrow::editor::EditorSession::EditTransaction transaction;
};

struct TimelineEditorState {
    double frames_per_second{60.0};
    bool snap_to_frames{true};
    double view_start_seconds{0.0};
    double pixels_per_second{160.0};
    std::vector<TimelineKeyRef> selected_keys;
    TimelineClipboard clipboard;
    std::optional<TimelineBoxSelection> box_selection;
    std::optional<TimelineRetimeGesture> retime_gesture;
};

using AgentReviewKind = marrow::editor::AgentReviewKind;
using AgentReviewRequest = marrow::editor::AgentReviewRequest;
using AgentActivityEntry = marrow::editor::AgentActivityEntry;

struct ShellState {
    ShellState()
        : load_result(marrow::editor::EditorSessionShellBinding::load_result(session)) {}

    ShellState(const ShellState&) = delete;
    ShellState& operator=(const ShellState&) = delete;
    ShellState(ShellState&&) = delete;
    ShellState& operator=(ShellState&&) = delete;

    // UI-free authoring and agent state. Remaining fields own presentation,
    // selection, gestures, platform resources, and synchronized preview views.
    marrow::editor::EditorSession session;
    std::uint64_t observed_project_revision{0U};
    std::uint64_t observed_runtime_revision{0U};
    std::uint64_t observed_preview_revision{0U};
    std::filesystem::path project_path;
    marrow::editor::ViewportState viewport{};
    ViewportCamera viewport_camera{};
    bool hud_overlay_enabled{false};
    ShellMode shell_mode{ShellMode::Animation};
    WeightPaintSettings weight_paint{};
    marrow::editor::ProjectLoadResult& load_result;
    std::optional<PendingEditAction> pending_edit_action;
    std::optional<AnimationDurationGesture> animation_duration_gesture;
    std::optional<InspectorTransformGesture> inspector_transform_gesture;
    std::optional<ViewportTransformGesture> viewport_transform_gesture;
    std::optional<ViewportBoxSelectionGesture> viewport_box_selection;
    std::optional<ParameterSliderGesture> parameter_slider_gesture;
    std::optional<ParameterGeometryGesture> parameter_geometry_gesture;
    TimelineEditorState timeline_editor{};
    TimelineTrackCache timeline_track_cache{};
    SlotDerivedCache slot_derived_cache{};
    MeshWeightStrokeState weight_paint_stroke{};
    PointerMediator pointer_mediator{};
    ViewportRenderResources viewport_renderer{};
    DockLayoutState dock_layout{};
    marrow::runtime::Skeleton* preview_skeleton{nullptr};
    marrow::runtime::AnimationState* animation_state{nullptr};
    marrow::editor::SelectionSet selection;
    std::optional<marrow::editor::SelectionItem> hierarchy_selection_anchor;
    std::optional<std::string> selected_timeline_track_id;
    std::vector<std::string> preview_skin_names;
    std::vector<std::optional<PreviewAttachmentSelection>> preview_slot_overrides;
    std::string selected_animation_name;
    double timeline_time_seconds{0.0};
    bool timeline_loop{true};
    bool timeline_playing{false};
    bool preview_queue_enabled{false};
    std::string preview_queued_animation_name;
    double preview_queue_delay{0.0};
    bool preview_use_custom_mix_duration{false};
    double preview_custom_mix_duration{0.0};
    bool preview_reverse{false};
    marrow::runtime::RootMotionDelta preview_root_motion_delta{};
    marrow::runtime::RootMotionDelta preview_root_motion_total{};
    std::vector<marrow::runtime::AnimationEvent> preview_events;
    bool export_binary_output{false};
    bool project_dirty{false};
    bool default_dock_layout_initialized{false};
    std::string saved_project_snapshot;
    std::string status_message;
    std::string error_message;
    std::vector<RuntimeAssetWatchEntry> runtime_asset_watch_entries;
    /** Accumulates frame time so hot-reload stat() polling runs at ~4 Hz
        instead of every frame. */
    double runtime_asset_watch_accumulator_seconds{0.0};
    std::optional<marrow::runtime::ProfilerFrame> hud_overlay_frame;
    marrow::editor::IconRegistry icons{};
    std::array<char, 128> hierarchy_filter{};
    // Active Constraints type tab: 0=IK 1=Path 2=Transform 3=Physics.
    int constraints_tab{0};
    // Agent surface — optional, closed by default (Ctrl+L / toolbar toggle).
    // The socket can be turned on/off at runtime from the panel.
    AgentSocketServer* agent_server{nullptr};
    std::optional<int> agent_listen_port;  // last/CLI port
    std::string agent_token;               // from --agent-token (optional)
    bool show_agent_panel{false};
    bool agent_panel_was_open{false};
    marrow::editor::AgentControlState agent_control{};
};

// This member list must stay in step with cancel_authoring_gestures
// (shell_core.cpp): the predicate gates every begin-gesture path, and the
// cancel list releases the matching live transactions.
inline bool authoring_gesture_active(const ShellState& state) noexcept {
    return state.pending_edit_action.has_value() ||
        state.animation_duration_gesture.has_value() ||
        state.inspector_transform_gesture.has_value() ||
        state.viewport_transform_gesture.has_value() ||
        state.parameter_slider_gesture.has_value() ||
        state.parameter_geometry_gesture.has_value() ||
        state.timeline_editor.retime_gesture.has_value() ||
        state.weight_paint_stroke.active;
}

void sync_shell_from_editor_session(ShellState* state);
void sync_shell_from_editor_session_if_revised(ShellState* state);

inline marrow::editor::AgentDispatchResult dispatch_agent_command(
    ShellState* state,
    const marrow::runtime::json::Value& command) {
    if (state == nullptr) {
        marrow::editor::AgentDispatchResult result;
        result.message = "Editor shell state is unavailable.";
        result.error_code = "invalid_request";
        return result;
    }
    if (authoring_gesture_active(*state)) {
        state->status_message = "Finish the active edit before running another command";
        marrow::editor::AgentDispatchResult result;
        result.message = state->status_message;
        result.error_code = "blocked";
        return result;
    }
    marrow::editor::AgentCommandContext context{state->session, state->agent_control};
    marrow::editor::AgentDispatchResult result =
        marrow::editor::AgentCommandDispatcher::dispatch(context, command);
    sync_shell_from_editor_session(state);
    return result;
}

constexpr int kDefaultAgentPort = 9876;

constexpr char kProjectWindowTitle[] = "Project";
constexpr char kRuntimeAssetsWindowTitle[] = "Runtime Assets";
constexpr char kConstraintsWindowTitle[] = "Constraints";
constexpr char kHierarchyWindowTitle[] = "Hierarchy";
constexpr char kTimelineWindowTitle[] = "Timeline";
constexpr char kViewportWindowTitle[] = "Viewport";
constexpr char kPropertiesWindowTitle[] = "Properties";
constexpr char kAgentWindowTitle[] = "Agent";
constexpr char kParametersWindowTitle[] = "Parameters";
constexpr char kParameterDeformersWindowTitle[] = "Shapes / Deformers";
constexpr char kExpressionsWindowTitle[] = "Expressions";
constexpr char kLipSyncWindowTitle[] = "Lip Sync";
constexpr float kBoneJointHitRadiusPixels = 6.0f;
constexpr float kBoneBodyHitThresholdPixels = 8.0f;
constexpr float kPi = 3.14159265358979323846f;
constexpr double kOnionSkinFrameRate = 60.0;
constexpr double kOnionSkinFrameDuration = 1.0 / kOnionSkinFrameRate;
// Shared shell/session helpers.
// selected_bone_index/selected_bone_name/selected_attachment resolve the
// CONTEXT entity (slot/attachment selections fall back to owners); production
// consumers migrated to ResolvedSelection's active-item fields (MAR-158) and
// only shell_smoke still observes the context contract through these.
std::optional<std::size_t> selected_bone_index(const ShellState& state);
std::optional<std::string_view> selected_bone_name(const ShellState& state);
std::optional<std::size_t> selected_slot_index(const ShellState& state);
std::optional<PreviewAttachmentSelection> selected_attachment(const ShellState& state);
std::optional<marrow::editor::ConstraintSelection> selected_constraint(
    const ShellState& state);
const marrow::runtime::AnimationData* selected_animation(const ShellState& state);
double selected_animation_duration(const ShellState& state);
const marrow::runtime::AnimationData* queued_preview_animation(const ShellState& state);
std::string default_queued_preview_animation_name(const ShellState& state);
void normalize_state_preview_settings(ShellState* state);
double timeline_preview_duration(const ShellState& state);
std::vector<std::string> normalize_preview_skin_names(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names);

EditorHistorySnapshot capture_history_snapshot(
    const ShellState& state,
    bool include_serialized_project = true);
bool history_snapshots_equal(
    const EditorHistorySnapshot& left,
    const EditorHistorySnapshot& right);
void restore_history_snapshot(
    ShellState* state,
    const EditorHistorySnapshot& snapshot);
bool record_action_from_snapshots(
    ShellState* state,
    const EditorHistorySnapshot& before,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge);
void cancel_authoring_gestures(ShellState* state, std::string_view reason);
bool rebuild_project_runtime(ShellState* state);
void update_project_dirty_state(ShellState* state);
bool save_project_file(ShellState* state, bool update_status_message);
bool export_runtime_assets_file(ShellState* state, bool update_status_message);
bool reload_project(ShellState* state);

// Viewport renderer and geometry helpers.
std::optional<std::string> initialize_viewport_renderer(
    ViewportRenderResources* resources);
void destroy_viewport_renderer(ViewportRenderResources* resources);
ViewportFramebufferSize viewport_framebuffer_size(
    const ImVec2& canvas_size,
    const ImVec2& framebuffer_scale);
std::optional<std::string> ensure_viewport_framebuffer(
    ViewportRenderResources* resources,
    int width,
    int height);
std::optional<std::string> render_prepared_scene_framebuffer(
    const ViewportLayout& layout,
    const ViewportGeometryPass& background_geometry,
    const ViewportGeometryPass& overlay_geometry,
    const std::vector<OnionSkinTexturedGhost>& textured_ghosts,
    const marrow::renderer::PreparedScene& scene,
    const std::filesystem::path& atlas_image_path,
    ViewportRenderResources* resources);
std::optional<std::string> render_viewport_framebuffer(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    const marrow::renderer::PreparedScene* prepared_scene,
    ViewportRenderResources* resources);
std::optional<ViewportLayout> build_viewport_layout(
    const ShellState& state,
    const ImVec2& canvas_origin,
    const ImVec2& canvas_size);
bool initialize_viewport_camera_from_preview_pose(ShellState* state);
bool frame_viewport_camera_to_preview_pose(ShellState* state);
bool zoom_viewport_at_screen_position(
    ShellState* state,
    const ImVec2& canvas_origin,
    const ImVec2& canvas_size,
    const ImVec2& screen_position,
    double zoom_factor);
std::vector<OnionSkinGhostPose> build_onion_skin_ghost_poses(
    const ShellState& state,
    const ViewportLayout& layout);
void build_viewport_render_geometry(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    std::vector<ViewportRenderVertex>* line_vertices,
    std::vector<ViewportRenderVertex>* triangle_vertices);
DebugOverlayGeometry build_debug_overlay_geometry(
    const ShellState& state,
    const ViewportLayout& layout);
float first_grid_line(float anchor, float minimum, float spacing);
float squared_distance(const ImVec2& a, const ImVec2& b);
float point_segment_distance_squared(
    const ImVec2& point,
    const ImVec2& segment_start,
    const ImVec2& segment_end);
ImVec2 screen_from_world(
    const ViewportLayout& layout,
    double world_x,
    double world_y);
ImVec2 screen_from_world(
    const ViewportLayout& layout,
    float world_x,
    float world_y);
ViewportWorldPoint world_from_screen(
    const ViewportLayout& layout,
    const ImVec2& screen_position);
ImVec2 local_viewport_position(
    const ViewportLayout& layout,
    const ImVec2& screen_position);
std::array<float, 16> viewport_projection_matrix(const ViewportLayout& layout);
std::filesystem::path resolve_viewport_atlas_image_path(
    const ShellState& state,
    const marrow::renderer::PreparedScene& scene);
ViewportRenderVertex viewport_vertex(const ImVec2& position, const ImVec4& color);
void append_colored_line(
    const ImVec2& start,
    const ImVec2& end,
    const ImVec4& color,
    std::vector<ViewportRenderVertex>* vertices);
void append_filled_circle(
    const ImVec2& center,
    float radius,
    const ImVec4& fill_color,
    const ImVec4& outline_color,
    int segments,
    std::vector<ViewportRenderVertex>* triangle_vertices,
    std::vector<ViewportRenderVertex>* line_vertices);
void append_viewport_pose_geometry(
    const ViewportLayout& layout,
    const std::vector<BoneCanvasNode>& bones,
    float joint_radius,
    const std::vector<bool>* selected_bones,
    std::optional<std::size_t> active_bone,
    std::optional<std::size_t> hovered_bone,
    ImU32 line_color,
    ImU32 joint_fill_color,
    ImU32 joint_outline_color,
    ImU32 selected_line_color,
    ImU32 selected_fill_color,
    ImU32 selected_outline_color,
    ImU32 hovered_outline_color,
    std::vector<ViewportRenderVertex>* line_vertices,
    std::vector<ViewportRenderVertex>* triangle_vertices);
void build_viewport_background_geometry(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    ViewportGeometryPass* geometry);
void build_viewport_overlay_geometry(
    const ShellState& state,
    const ViewportLayout& layout,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ViewportGeometryPass* geometry);
std::optional<std::size_t> pick_bone_at_position(
    const ViewportLayout& layout,
    const ImVec2& position);
ViewportEntityHitGeometry build_viewport_entity_hit_geometry(
    const ShellState& state,
    const ViewportLayout& layout,
    const marrow::renderer::PreparedScene* prepared_scene);
std::optional<ViewportEntityHitCandidate> resolve_viewport_entity_hit_candidates(
    const std::vector<ViewportEntityHitCandidate>& candidates);
std::optional<ViewportEntityHitCandidate> pick_viewport_entity_at_position(
    const ShellState& state,
    const ViewportLayout& layout,
    const ViewportEntityHitGeometry& geometry,
    const ImVec2& position);
std::vector<marrow::editor::SelectionItem> collect_viewport_box_bones(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& first_corner,
    const ImVec2& second_corner);
double onion_skin_alpha(int distance_rank, int total_count);

void ensure_default_dock_layout(
    ShellState* state,
    ImGuiID dockspace_id,
    const ImGuiViewport* viewport);
void apply_editor_theme();

int run_headless_smoke(const Options& options);
#if defined(__APPLE__)
int run_launch_focus_verification();
#endif

} // namespace marrow::editor::shell
