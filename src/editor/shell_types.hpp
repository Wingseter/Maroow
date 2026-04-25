#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <algorithm>

struct GLFWwindow;

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include "imgui.h"

#include "icon_registry.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/profiler.hpp"

namespace marrow::editor::shell {

struct Options {
    std::filesystem::path project_path{"assets/fixtures/player_idle.marrow"};
    std::optional<int> auto_close_frames;
    bool verify_launch_focus{false};
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
    GLuint framebuffer{0};
    GLuint color_texture{0};
    GLuint depth_stencil_renderbuffer{0};
    GLuint program{0};
    GLuint vertex_shader{0};
    GLuint fragment_shader{0};
    GLuint vao{0};
    GLuint vbo{0};
    marrow::editor::ViewportRenderer prepared_scene_renderer{};
    GLint view_size_location{-1};
    int framebuffer_width{0};
    int framebuffer_height{0};
    std::string error_message;
};

struct RuntimeAssetWatchEntry {
    std::filesystem::path path;
    bool exists{false};
    std::optional<std::filesystem::file_time_type> write_time;
};

struct DockLayoutState {
    ImGuiID dockspace_id{0};
    ImGuiID viewport_node_id{0};
    ImGuiID timeline_node_id{0};
    ImGuiID hierarchy_node_id{0};
    ImGuiID properties_node_id{0};
};

struct AttachmentSelection {
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

enum class ConstraintEditKind {
    Ik,
    Path,
    Transform,
    Physics,
};

struct ConstraintSelection {
    ConstraintEditKind kind{ConstraintEditKind::Ik};
    std::string name;
};

struct ShellState;

struct EditorHistorySnapshot {
    marrow::editor::ProjectData project;
    std::string serialized_project;
    std::vector<std::string> preview_skin_names;
    std::vector<std::optional<AttachmentSelection>> preview_slot_overrides;
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

class EditAction {
public:
    EditAction(
        EditActionKind kind,
        std::string label,
        std::string group,
        bool allow_merge);
    virtual ~EditAction() = default;

    EditActionKind kind() const;
    const std::string& label() const;
    const std::string& group() const;
    bool allow_merge() const;

    virtual bool undo(ShellState* state) const = 0;
    virtual bool redo(ShellState* state) const = 0;
    virtual bool merge_from(const EditAction& action) = 0;

protected:
    EditActionKind kind_;
    std::string label_;
    std::string group_;
    bool allow_merge_{false};
};

bool apply_history_snapshot(ShellState* state, const EditorHistorySnapshot& snapshot);

class SnapshotEditAction : public EditAction {
public:
    SnapshotEditAction(
        EditActionKind kind,
        std::string label,
        std::string group,
        bool allow_merge,
        EditorHistorySnapshot before,
        EditorHistorySnapshot after);

    bool undo(ShellState* state) const override;
    bool redo(ShellState* state) const override;
    bool merge_from(const EditAction& action) override;

private:
    EditorHistorySnapshot before_;
    EditorHistorySnapshot after_;
};

class MoveBoneAction final : public SnapshotEditAction {
public:
    using SnapshotEditAction::SnapshotEditAction;
};

class AddKeyframeAction final : public SnapshotEditAction {
public:
    using SnapshotEditAction::SnapshotEditAction;
};

class RemoveKeyframeAction final : public SnapshotEditAction {
public:
    using SnapshotEditAction::SnapshotEditAction;
};

class EditPropertyAction final : public SnapshotEditAction {
public:
    using SnapshotEditAction::SnapshotEditAction;
};

class UndoStack {
public:
    bool can_undo() const;
    bool can_redo() const;
    std::size_t undo_count() const;
    std::size_t redo_count() const;
    void clear();
    const EditAction* peek_undo() const;
    const EditAction* peek_redo() const;
    void push(std::unique_ptr<EditAction> action);
    bool undo(ShellState* state, std::string* label_out);
    bool redo(ShellState* state, std::string* label_out);

private:
    static constexpr std::size_t kMaxDepth = 100U;

    std::vector<std::unique_ptr<EditAction>> undo_actions_;
    std::vector<std::unique_ptr<EditAction>> redo_actions_;
};

struct PendingEditAction {
    ImGuiID item_id{0};
    EditActionKind kind{EditActionKind::EditProperty};
    std::string label;
    std::string group;
    bool allow_merge{false};
    EditorHistorySnapshot before_snapshot;
};

struct ShellState {
    std::filesystem::path project_path;
    marrow::editor::ViewportState viewport{};
    bool hud_overlay_enabled{false};
    WeightPaintSettings weight_paint{};
    marrow::editor::ProjectLoadResult load_result{};
    UndoStack command_stack;
    std::optional<PendingEditAction> pending_edit_action;
    MeshWeightStrokeState weight_paint_stroke{};
    ViewportRenderResources viewport_renderer{};
    DockLayoutState dock_layout{};
    marrow::UniquePtr<marrow::runtime::Skeleton> preview_skeleton;
    marrow::UniquePtr<marrow::runtime::AnimationState> animation_state;
    std::optional<std::size_t> selected_bone_index;
    std::optional<std::size_t> selected_slot_index;
    std::optional<AttachmentSelection> selected_attachment;
    std::optional<std::string> selected_timeline_track_id;
    std::optional<ConstraintSelection> selected_constraint;
    std::vector<std::string> preview_skin_names;
    std::vector<std::optional<AttachmentSelection>> preview_slot_overrides;
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
    std::optional<marrow::runtime::ProfilerFrame> hud_overlay_frame;
    marrow::editor::IconRegistry icons{};
    std::array<char, 128> hierarchy_filter{};
};

constexpr char kProjectWindowTitle[] = "Project";
constexpr char kRuntimeAssetsWindowTitle[] = "Runtime Assets";
constexpr char kConstraintsWindowTitle[] = "Constraints";
constexpr char kHierarchyWindowTitle[] = "Hierarchy";
constexpr char kTimelineWindowTitle[] = "Timeline";
constexpr char kViewportWindowTitle[] = "Viewport";
constexpr char kPropertiesWindowTitle[] = "Properties";
constexpr float kBoneJointHitRadiusPixels = 6.0f;
constexpr float kBoneBodyHitThresholdPixels = 8.0f;
constexpr ImVec2 kViewportImageUv0{0.0f, 1.0f};
constexpr ImVec2 kViewportImageUv1{1.0f, 0.0f};
constexpr float kPi = 3.14159265358979323846f;
constexpr double kOnionSkinFrameRate = 60.0;
constexpr double kOnionSkinFrameDuration = 1.0 / kOnionSkinFrameRate;
constexpr const char* kViewportVertexShaderSource = R"(#version 150
in vec2 a_position;
in vec4 a_color;

uniform vec2 u_view_size;

out vec4 v_color;

void main() {
    vec2 normalized_position = vec2(
        (a_position.x / u_view_size.x) * 2.0 - 1.0,
        1.0 - ((a_position.y / u_view_size.y) * 2.0));
    v_color = a_color;
    gl_Position = vec4(normalized_position, 0.0, 1.0);
}
)";

constexpr const char* kViewportFragmentShaderSource = R"(#version 150
in vec4 v_color;

out vec4 frag_color;

void main() {
    frag_color = v_color;
}
)";

// Forward declarations
const char* yes_no(bool value);
std::optional<std::string_view> default_skin_name(
    const marrow::runtime::SkeletonData& skeleton);
bool is_default_skin_index(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t skin_index);
std::string source_skin_name(
    const marrow::runtime::SkeletonData& skeleton,
    std::optional<std::size_t> skin_index);
const char* blend_mode_name(marrow::runtime::BlendMode blend_mode);
const char* attachment_kind_name(marrow::runtime::AttachmentKind kind);
const char* sequence_playback_mode_name(marrow::runtime::SequencePlaybackMode mode);
const char* onion_skin_mode_name(marrow::editor::OnionSkinMode mode);
std::string format_slot_color(const marrow::runtime::SlotColor& color);
std::vector<std::string> normalize_preview_skin_names(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& skin_names);
std::string preview_skin_summary(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names);
std::vector<SlotAttachmentReference> collect_slot_attachments(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index);
std::optional<SlotAttachmentReference> resolve_attachment_reference(
    const marrow::runtime::SkeletonData& skeleton,
    const AttachmentSelection& selection);
std::optional<AttachmentSelection> current_attachment_selection(
    const ShellState& state,
    std::size_t slot_index);
std::vector<MeshWeightVertexRow> build_mesh_weight_rows(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AttachmentData& attachment);
std::optional<AttachmentSelection> first_attachment_selection_for_slot(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index);
std::vector<std::size_t> build_active_preview_skin_indices(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names);
std::optional<AttachmentSelection> resolve_skin_preview_attachment(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names,
    std::size_t slot_index);
bool attachment_matches_selection(
    const AttachmentSelection& selection,
    const SlotAttachmentReference& reference);
std::optional<std::size_t> draw_order_position(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t slot_index);
const marrow::runtime::AnimationData* selected_animation(const ShellState& state);
double selected_animation_duration(const ShellState& state);
const marrow::runtime::AnimationData* queued_preview_animation(const ShellState& state);
std::string default_queued_preview_animation_name(const ShellState& state);
void normalize_state_preview_settings(ShellState* state);
double timeline_preview_duration(const ShellState& state);
std::string format_time_seconds(double time_seconds);
std::vector<TimelineTrackRow> build_timeline_tracks(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AnimationData& animation);
const TimelineTrackRow* selected_timeline_track(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);
bool timeline_track_matches_selection(
    const ShellState& state,
    const TimelineTrackRow& track);
const char* constraint_kind_label(ConstraintEditKind kind);
void draw_constraints_window(ShellState* state);
EditorHistorySnapshot capture_history_snapshot(
    const ShellState& state,
    bool include_serialized_project = true);
bool history_snapshots_equal(
    const EditorHistorySnapshot& left,
    const EditorHistorySnapshot& right);
void restore_history_snapshot(
    ShellState* state,
    const EditorHistorySnapshot& snapshot);
std::unique_ptr<EditAction> make_edit_action(
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    const EditorHistorySnapshot& before,
    const EditorHistorySnapshot& after);
bool record_action_from_snapshots(
    ShellState* state,
    const EditorHistorySnapshot& before,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge);
bool rebuild_project_runtime(ShellState* state);
std::vector<std::filesystem::path> current_runtime_asset_paths(const ShellState& state);
void reset_runtime_asset_watch(ShellState* state);
bool apply_current_animation_state_to_preview(ShellState* state);
bool restore_preview_playback(
    ShellState* state,
    const marrow::runtime::AnimationStateSnapshot& snapshot);
bool reload_runtime_source_assets(ShellState* state);
enum class RuntimeAssetPollOutcome {
    Unchanged,
    Reloaded,
    Failed,
};
RuntimeAssetPollOutcome poll_runtime_asset_changes(ShellState* state);
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
    ViewportRenderResources* resources);
void draw_viewport_fallback_scene(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list);
void draw_viewport_annotations(
    const ShellState& state,
    const ViewportLayout& layout,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list);
void ensure_default_dock_layout(
    ShellState* state,
    ImGuiID dockspace_id,
    const ImGuiViewport* viewport);
void update_project_dirty_state(ShellState* state);
bool apply_project_command_change(
    ShellState* state,
    const marrow::editor::ProjectData& previous_project,
    EditActionKind kind,
    std::string command_label,
    std::string group,
    bool allow_merge,
    std::string failure_status);
bool undo_project_change(ShellState* state);
bool redo_project_change(ShellState* state);
void handle_project_history_shortcuts(ShellState* state);
float squared_distance(const ImVec2& a, const ImVec2& b);
template <typename MutateFn>
bool execute_viewport_setting_edit_action(
    ShellState* state,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate);
template <typename MutateFn>
bool apply_coalesced_viewport_drag(
    ShellState* state,
    bool changed,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate);
bool save_project_file(ShellState* state, bool update_status_message);
bool export_runtime_assets_file(ShellState* state, bool update_status_message);
void apply_preview_slot_overrides(
    const ShellState& state,
    marrow::runtime::Skeleton* skeleton);
void apply_preview_slot_overrides(ShellState* state);
bool refresh_preview_pose(ShellState* state);
bool set_selected_animation(
    ShellState* state,
    std::string_view animation_name,
    std::string_view source,
    bool update_status_message,
    bool reset_time);
std::optional<marrow::runtime::ProfilerFrame> build_preview_profiler_frame(
    const ShellState& state);
ImVec2 screen_from_world(
    const ViewportLayout& layout,
    double world_x,
    double world_y);
ImVec2 screen_from_world(
    const ViewportLayout& layout,
    float world_x,
    float world_y);
std::array<float, 16> viewport_projection_matrix(const ViewportLayout& layout);
std::filesystem::path resolve_viewport_atlas_image_path(
    const ShellState& state,
    const marrow::renderer::PreparedScene& scene);
ImVec2 local_viewport_position(
    const ViewportLayout& layout,
    const ImVec2& screen_position);
bool scrub_timeline_time(
    ShellState* state,
    double time_seconds,
    std::string_view source,
    bool update_status_message);
void advance_timeline_playback(ShellState* state, double delta_seconds);
void advance_timeline_playback(ShellState* state, float delta_seconds);
bool focus_timeline_track(
    ShellState* state,
    const TimelineTrackRow& track,
    double time_seconds,
    std::string_view source,
    bool update_status_message);
std::vector<double> collect_animation_key_times(const marrow::runtime::AnimationData& animation);
std::vector<OnionSkinGhostPose> build_onion_skin_ghost_poses(
    const ShellState& state,
    const ViewportLayout& layout);
void draw_draw_order_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_event_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_mesh_deform_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_transform_timeline_editor(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);

// Viewport functions (shell_viewport.cpp)
std::optional<ViewportLayout> build_viewport_layout(
    const ShellState& state,
    const ImVec2& canvas_origin,
    const ImVec2& canvas_size);
std::optional<MeshWeightOverlay> build_mesh_weight_overlay(
    const ShellState& state,
    const ViewportLayout& layout);
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

// Smoke functions (shell_smoke.cpp)
int run_headless_smoke(const Options& options);
#if defined(__APPLE__)
int run_launch_focus_verification();
#endif

// Viewport functions (shell_viewport.cpp)
float first_grid_line(float anchor, float minimum, float spacing);
float squared_distance(const ImVec2& a, const ImVec2& b);
float point_segment_distance_squared(
    const ImVec2& point,
    const ImVec2& segment_start,
    const ImVec2& segment_end);
ImVec2 local_viewport_position(
    const ViewportLayout& layout,
    const ImVec2& screen_position);
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
    std::optional<std::size_t> selected_bone,
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
double onion_skin_alpha(int distance_rank, int total_count);

// Weight paint utilities (shell_main.cpp)
ImVec4 mesh_weight_heatmap_color(double weight, float alpha = 0.55f);

// Functions in shell_main.cpp called by shell_smoke.cpp
void select_bone(
    ShellState* state,
    std::optional<std::size_t> bone_index,
    std::string_view source,
    bool update_status_message);
bool set_preview_skin_enabled(
    ShellState* state,
    std::size_t skin_index,
    bool enabled,
    bool update_status_message,
    bool record_history = true);
bool reload_project(ShellState* state);
bool rebuild_project_runtime(ShellState* state);
void apply_editor_theme();
void materialize_temp_project_runtime_assets(
    const ShellState& state,
    marrow::editor::ProjectData* project);
std::optional<std::size_t> ensure_transform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_draw_order_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);

void select_slot(
    ShellState* state,
    std::optional<std::size_t> slot_index,
    std::string_view source,
    bool update_status_message);
void select_attachment(
    ShellState* state,
    std::optional<AttachmentSelection> selection,
    std::string_view source,
    bool update_status_message);
bool apply_attachment_selection_to_preview_slot(
    ShellState* state,
    const AttachmentSelection& selection,
    std::string_view source,
    bool update_status_message,
    bool record_history = true);
bool reset_preview_slot_to_skin_selection(
    ShellState* state,
    std::size_t slot_index,
    std::string_view source,
    bool update_status_message,
    bool record_history = true);
double weight_for_bone(
    const marrow::runtime::MeshGeometry::VertexWeights& vertex_weights,
    std::size_t bone_index);
std::optional<MeshWeightPaintTarget> current_mesh_weight_paint_target(
    const ShellState& state);
void reset_weight_paint_stroke(ShellState* state);
void begin_weight_paint_stroke(
    ShellState* state,
    const MeshWeightPaintTarget& target);
bool finish_weight_paint_stroke(ShellState* state);
bool apply_weight_paint_sample(
    ShellState* state,
    const MeshWeightOverlay& overlay,
    const ImVec2& screen_position);
std::optional<std::size_t> ensure_mesh_deform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_event_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);

// Constraint functions (shell_main.cpp)
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

// Additional functions (shell_main.cpp) called by shell_smoke.cpp
bool apply_preview_skin_selection(
    ShellState* state,
    std::string_view source,
    bool update_status_message);
marrow::editor::TransformKeyframeEdit sample_transform_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track);
template <typename Keyframe>
std::optional<double> insertable_key_time(
    const std::vector<Keyframe>& keyframes,
    double desired_time,
    double duration) {
    constexpr double kKeySpacing = 0.001;
    auto iterator = std::upper_bound(
        keyframes.begin(),
        keyframes.end(),
        desired_time,
        [](double time, const Keyframe& keyframe) {
            return time < keyframe.time;
        });
    const double minimum_time =
        iterator == keyframes.begin() ? 0.0 : (iterator - 1)->time + kKeySpacing;
    if (iterator == keyframes.end()) {
        return std::max(desired_time, minimum_time);
    }
    const double maximum_time = iterator->time - kKeySpacing;
    if (maximum_time < minimum_time) {
        return std::nullopt;
    }
    return std::clamp(desired_time, minimum_time, maximum_time);
}

// Draw functions (shell_main.cpp)
void draw_menu_bar(GLFWwindow* window, bool* reload_requested, ShellState* state);
void draw_project_window(bool* reload_requested, ShellState* state);
void draw_runtime_window(const ShellState& state);
void draw_timeline_window(ShellState* state);
void draw_hierarchy_window(ShellState* state);
void draw_viewport_window(ShellState* state);
void draw_inspector_window(ShellState* state);

} // namespace marrow::editor::shell
