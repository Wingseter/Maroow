#include <array>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include "marrow/editor/module.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/runtime/animation_state.hpp"

namespace {

struct Options {
    std::filesystem::path project_path{"assets/fixtures/player_idle.marrow"};
    std::optional<int> auto_close_frames;
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
    float joint_radius{6.0f};
    std::vector<BoneCanvasNode> bones;
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

struct ShellState {
    std::filesystem::path project_path;
    marrow::editor::ViewportState viewport{};
    marrow::editor::ProjectLoadResult load_result{};
    std::unique_ptr<marrow::runtime::Skeleton> preview_skeleton;
    std::unique_ptr<marrow::runtime::AnimationState> animation_state;
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
    std::string status_message;
    std::string error_message;
};

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
bool rebuild_project_runtime(ShellState* state);
bool save_project_file(ShellState* state, bool update_status_message);
bool export_runtime_assets_file(ShellState* state, bool update_status_message);
bool refresh_preview_pose(ShellState* state);
bool set_selected_animation(
    ShellState* state,
    std::string_view animation_name,
    std::string_view source,
    bool update_status_message,
    bool reset_time);
bool scrub_timeline_time(
    ShellState* state,
    double time_seconds,
    std::string_view source,
    bool update_status_message);
void advance_timeline_playback(ShellState* state, double delta_seconds);
bool focus_timeline_track(
    ShellState* state,
    const TimelineTrackRow& track,
    double time_seconds,
    std::string_view source,
    bool update_status_message);
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

void print_usage(std::string_view executable_name) {
    std::cout << "Usage: " << executable_name
              << " [project.marrow] [--auto-close <frames>]\n"
                 "       "
              << executable_name
              << " --project <project.marrow> [--auto-close <frames>]\n"
                 "Launch the Marrow Dear ImGui editor shell using GLFW and OpenGL.\n";
}

std::optional<int> parse_positive_integer(const char* text) {
    try {
        const int value = std::stoi(text);
        if (value <= 0) {
            return std::nullopt;
        }
        return value;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

ParseResult parse_arguments(int argc, char** argv) {
    ParseResult result;
    bool project_path_set = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            print_usage(argv[0]);
            result.status = ParseStatus::Help;
            return result;
        }

        if (argument == "--project") {
            if (index + 1 >= argc) {
                std::cerr << "--project requires a .marrow path.\n";
                print_usage(argv[0]);
                return result;
            }

            result.options.project_path = std::filesystem::path(argv[++index]);
            project_path_set = true;
            continue;
        }

        if (argument == "--auto-close") {
            if (index + 1 >= argc) {
                std::cerr << "--auto-close requires a positive frame count.\n";
                print_usage(argv[0]);
                return result;
            }

            const std::optional<int> value = parse_positive_integer(argv[++index]);
            if (!value.has_value()) {
                std::cerr << "--auto-close expects a positive integer.\n";
                print_usage(argv[0]);
                return result;
            }

            result.options.auto_close_frames = value;
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "Unknown option: " << argument << '\n';
            print_usage(argv[0]);
            return result;
        }

        if (project_path_set) {
            std::cerr << "Only one project path may be provided.\n";
            print_usage(argv[0]);
            return result;
        }

        result.options.project_path = std::filesystem::path(argument);
        project_path_set = true;
    }

    result.status = ParseStatus::Ok;
    return result;
}

void glfw_error_callback(int error_code, const char* description) {
    std::cerr << "GLFW error " << error_code << ": " << description << '\n';
}

void configure_glfw_for_editor() {
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_SAMPLES, 4);
}

float monitor_content_scale() {
    GLFWmonitor* primary_monitor = glfwGetPrimaryMonitor();
    if (primary_monitor == nullptr) {
        return 1.0f;
    }

    return ImGui_ImplGlfw_GetContentScaleForMonitor(primary_monitor);
}

std::string join_strings(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "<none>";
    }

    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << values[index];
    }
    return stream.str();
}

std::string join_paths(const std::vector<std::filesystem::path>& values) {
    if (values.empty()) {
        return "<none>";
    }

    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << values[index].string();
    }
    return stream.str();
}

std::filesystem::path absolutize_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path.lexically_normal();
    }

    std::error_code error;
    const std::filesystem::path current_directory = std::filesystem::current_path(error);
    if (error) {
        return path.lexically_normal();
    }

    return (current_directory / path).lexically_normal();
}

std::vector<std::filesystem::path> absolutize_paths(
    const std::vector<std::filesystem::path>& paths) {
    std::vector<std::filesystem::path> absolute_paths;
    absolute_paths.reserve(paths.size());
    for (const auto& path : paths) {
        absolute_paths.push_back(absolutize_path(path));
    }
    return absolute_paths;
}

void materialize_temp_project_runtime_assets(
    const ShellState& state,
    marrow::editor::ProjectData* project) {
    if (!state.load_result || state.load_result.project == nullptr || project == nullptr) {
        return;
    }

    project->runtime_assets.skeleton_path =
        absolutize_path(state.load_result.project->resolved_skeleton_path());
    project->runtime_assets.atlas_paths =
        absolutize_paths(state.load_result.project->resolved_atlas_paths());
}

void apply_editor_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* colors = style.Colors;

    colors[ImGuiCol_WindowBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.0f);
    colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
    colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.13f, 0.15f, 1.0f);
    colors[ImGuiCol_Header] = ImVec4(0.49f, 0.31f, 0.16f, 0.80f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.67f, 0.43f, 0.23f, 0.90f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.79f, 0.56f, 0.29f, 0.90f);
    colors[ImGuiCol_Button] = ImVec4(0.49f, 0.31f, 0.16f, 0.70f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.67f, 0.43f, 0.23f, 0.90f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.79f, 0.56f, 0.29f, 1.00f);
    colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.16f, 0.18f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.67f, 0.43f, 0.23f, 0.75f);
    colors[ImGuiCol_TabActive] = ImVec4(0.49f, 0.31f, 0.16f, 0.95f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.10f, 0.11f, 0.13f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.14f, 0.15f, 0.17f, 1.0f);
    colors[ImGuiCol_DockingPreview] = ImVec4(0.79f, 0.56f, 0.29f, 0.55f);

    style.WindowRounding = 6.0f;
    style.ChildRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(8.0f, 6.0f);
}

float clamp_zoom(float zoom) {
    return std::max(0.2f, std::min(zoom, 6.0f));
}

std::optional<std::string_view> selected_bone_name(const ShellState& state) {
    if (!state.load_result || !state.selected_bone_index.has_value()) {
        return std::nullopt;
    }

    const auto& bones = state.load_result.skeleton_data->bones();
    if (*state.selected_bone_index >= bones.size()) {
        return std::nullopt;
    }

    return bones[*state.selected_bone_index].name;
}

void select_bone(
    ShellState* state,
    std::optional<std::size_t> bone_index,
    std::string_view source,
    bool update_status_message) {
    state->selected_bone_index = bone_index;

    if (!update_status_message || !bone_index.has_value() || !state->load_result) {
        return;
    }

    const auto& bones = state->load_result.skeleton_data->bones();
    if (*bone_index >= bones.size()) {
        return;
    }

    std::ostringstream stream;
    stream << "Selected bone " << bones[*bone_index].name;
    if (!source.empty()) {
        stream << " via " << source;
    }
    state->status_message = stream.str();
}

void sync_attachment_selection_for_slot(ShellState* state, std::size_t slot_index) {
    if (!state->load_result) {
        state->selected_attachment.reset();
        return;
    }

    if (state->selected_attachment.has_value() &&
        state->selected_attachment->slot_index == slot_index &&
        resolve_attachment_reference(
            *state->load_result.skeleton_data,
            *state->selected_attachment).has_value()) {
        return;
    }

    if (const auto current_selection = current_attachment_selection(*state, slot_index)) {
        state->selected_attachment = current_selection;
        return;
    }

    state->selected_attachment =
        first_attachment_selection_for_slot(*state->load_result.skeleton_data, slot_index);
}

void select_attachment(
    ShellState* state,
    std::optional<AttachmentSelection> selection,
    std::string_view source,
    bool update_status_message) {
    state->selected_attachment = selection;
    if (!selection.has_value() || !state->load_result) {
        return;
    }

    state->selected_slot_index = selection->slot_index;
    if (selection->slot_index < state->load_result.skeleton_data->slots().size()) {
        state->selected_bone_index =
            state->load_result.skeleton_data->slots()[selection->slot_index].bone_index;
    }

    if (!update_status_message) {
        return;
    }

    std::ostringstream stream;
    stream << "Selected attachment " << selection->attachment_name;
    if (selection->skin_index.has_value()) {
        stream << " from skin "
               << source_skin_name(*state->load_result.skeleton_data, selection->skin_index);
    }
    if (!source.empty()) {
        stream << " via " << source;
    }
    state->status_message = stream.str();
}

void select_slot(
    ShellState* state,
    std::optional<std::size_t> slot_index,
    std::string_view source,
    bool update_status_message) {
    state->selected_slot_index = slot_index;
    if (!slot_index.has_value() || !state->load_result) {
        state->selected_attachment.reset();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    if (*slot_index >= skeleton.slots().size()) {
        state->selected_attachment.reset();
        return;
    }

    state->selected_bone_index = skeleton.slots()[*slot_index].bone_index;
    if (const auto current_selection = current_attachment_selection(*state, *slot_index)) {
        state->selected_attachment = current_selection;
    } else {
        state->selected_attachment =
            first_attachment_selection_for_slot(skeleton, *slot_index);
    }

    if (!update_status_message) {
        return;
    }

    std::ostringstream stream;
    stream << "Selected slot " << skeleton.slots()[*slot_index].name;
    if (!source.empty()) {
        stream << " via " << source;
    }
    state->status_message = stream.str();
}

bool apply_preview_skin_selection(
    ShellState* state,
    std::string_view source,
    bool update_status_message) {
    if (!state->load_result || !state->preview_skeleton) {
        return false;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    state->preview_skin_names =
        normalize_preview_skin_names(skeleton, state->preview_skin_names);

    if (!refresh_preview_pose(state)) {
        return false;
    }

    if (state->selected_slot_index.has_value()) {
        sync_attachment_selection_for_slot(state, *state->selected_slot_index);
    }

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Preview skins: "
               << preview_skin_summary(skeleton, state->preview_skin_names);
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}

const char* constraint_kind_label(ConstraintEditKind kind) {
    switch (kind) {
    case ConstraintEditKind::Ik:
        return "IK";
    case ConstraintEditKind::Path:
        return "Path";
    case ConstraintEditKind::Transform:
        return "Transform";
    case ConstraintEditKind::Physics:
        return "Physics";
    }

    return "Constraint";
}

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

std::optional<std::string> named_bone_if_exists(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view bone_name) {
    return skeleton.find_bone_index(bone_name).has_value()
        ? std::optional<std::string>(std::string(bone_name))
        : std::nullopt;
}

std::optional<std::string> named_slot_if_exists(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view slot_name) {
    return skeleton.find_slot_index(slot_name).has_value()
        ? std::optional<std::string>(std::string(slot_name))
        : std::nullopt;
}

std::vector<std::string> all_bone_names(const marrow::runtime::SkeletonData& skeleton) {
    std::vector<std::string> names;
    names.reserve(skeleton.bones().size());
    for (const auto& bone : skeleton.bones()) {
        names.push_back(bone.name);
    }
    return names;
}

std::vector<std::string> path_slot_names(const marrow::runtime::SkeletonData& skeleton) {
    std::vector<std::string> names;
    names.reserve(skeleton.slots().size());
    for (std::size_t slot_index = 0; slot_index < skeleton.slots().size(); ++slot_index) {
        const auto& slot = skeleton.slots()[slot_index];
        const auto* attachment =
            skeleton.find_attachment_source(slot_index, slot.setup_attachment);
        if (attachment != nullptr && attachment->path_attachment.has_value()) {
            names.push_back(slot.name);
        }
    }
    return names;
}

std::optional<std::string> first_non_root_bone_name(
    const marrow::runtime::SkeletonData& skeleton) {
    for (const auto& bone : skeleton.bones()) {
        if (bone.parent_index.has_value()) {
            return bone.name;
        }
    }
    if (!skeleton.bones().empty()) {
        return skeleton.bones().front().name;
    }
    return std::nullopt;
}

std::optional<std::string> first_constraint_target_name(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& excluded_bones) {
    for (const auto& bone : skeleton.bones()) {
        if (std::find(excluded_bones.begin(), excluded_bones.end(), bone.name) ==
            excluded_bones.end()) {
            return bone.name;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> first_child_bone_index(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t parent_index) {
    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        if (skeleton.bones()[bone_index].parent_index ==
            std::optional<std::size_t>{parent_index}) {
            return bone_index;
        }
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> preferred_chain(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string_view>& preferred_names) {
    std::vector<std::string> names;
    names.reserve(preferred_names.size());
    for (const std::string_view name : preferred_names) {
        if (!skeleton.find_bone_index(name).has_value()) {
            return std::nullopt;
        }
        names.emplace_back(name);
    }
    return names;
}

std::optional<std::vector<std::string>> first_direct_chain(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t length) {
    if (length == 0U || skeleton.bones().empty()) {
        return std::nullopt;
    }

    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        std::vector<std::string> chain;
        chain.push_back(skeleton.bones()[bone_index].name);
        std::size_t current_index = bone_index;
        while (chain.size() < length) {
            const auto child_index = first_child_bone_index(skeleton, current_index);
            if (!child_index.has_value()) {
                break;
            }
            chain.push_back(skeleton.bones()[*child_index].name);
            current_index = *child_index;
        }
        if (chain.size() == length) {
            return chain;
        }
    }

    return std::nullopt;
}

bool constraint_exists(
    const marrow::runtime::SkeletonData& skeleton,
    ConstraintEditKind kind,
    std::string_view name) {
    switch (kind) {
    case ConstraintEditKind::Ik:
        return find_named_constraint(skeleton.ik_constraints(), name) != nullptr;
    case ConstraintEditKind::Path:
        return find_named_constraint(skeleton.path_constraints(), name) != nullptr;
    case ConstraintEditKind::Transform:
        return find_named_constraint(skeleton.transform_constraints(), name) != nullptr;
    case ConstraintEditKind::Physics:
        return find_named_constraint(skeleton.physics_constraints(), name) != nullptr;
    }

    return false;
}

void validate_selected_constraint(ShellState* state) {
    if (!state->load_result || !state->selected_constraint.has_value()) {
        return;
    }
    if (!constraint_exists(
            *state->load_result.skeleton_data,
            state->selected_constraint->kind,
            state->selected_constraint->name)) {
        state->selected_constraint.reset();
    }
}

void select_constraint(
    ShellState* state,
    ConstraintEditKind kind,
    std::string_view name,
    std::string_view source,
    bool update_status_message) {
    state->selected_constraint = ConstraintSelection{kind, std::string(name)};
    if (!update_status_message) {
        return;
    }

    std::ostringstream stream;
    stream << "Selected " << constraint_kind_label(kind) << " constraint " << name;
    if (!source.empty()) {
        stream << " via " << source;
    }
    state->status_message = stream.str();
}

std::string unique_constraint_name(
    const ShellState& state,
    ConstraintEditKind kind,
    std::string_view prefix) {
    if (!state.load_result) {
        return std::string(prefix) + "_1";
    }

    for (int index = 1; index < 1000; ++index) {
        const std::string candidate = std::string(prefix) + "_" + std::to_string(index);
        if (!constraint_exists(*state.load_result.skeleton_data, kind, candidate)) {
            return candidate;
        }
    }

    return std::string(prefix) + "_overflow";
}

std::optional<marrow::editor::IkConstraintEdit> make_ik_constraint_edit_from_runtime(
    const ShellState& state,
    std::string_view name) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto* constraint =
        find_named_constraint(state.load_result.skeleton_data->ik_constraints(), name);
    if (constraint == nullptr) {
        return std::nullopt;
    }

    marrow::editor::IkConstraintEdit edit;
    edit.name = constraint->name;
    for (const std::size_t bone_index : constraint->bone_indices) {
        if (bone_index >= state.load_result.skeleton_data->bones().size()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(state.load_result.skeleton_data->bones()[bone_index].name);
    }
    if (constraint->target_bone_index >= state.load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }
    edit.target_bone_name =
        state.load_result.skeleton_data->bones()[constraint->target_bone_index].name;
    edit.mix = constraint->mix;
    edit.bend_positive = constraint->bend_positive;
    return edit;
}

std::optional<marrow::editor::PathConstraintEdit> make_path_constraint_edit_from_runtime(
    const ShellState& state,
    std::string_view name) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto* constraint =
        find_named_constraint(state.load_result.skeleton_data->path_constraints(), name);
    if (constraint == nullptr) {
        return std::nullopt;
    }

    marrow::editor::PathConstraintEdit edit;
    edit.name = constraint->name;
    if (constraint->slot_index >= state.load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }
    edit.slot_name = state.load_result.skeleton_data->slots()[constraint->slot_index].name;
    for (const std::size_t bone_index : constraint->bone_indices) {
        if (bone_index >= state.load_result.skeleton_data->bones().size()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(state.load_result.skeleton_data->bones()[bone_index].name);
    }
    edit.position = constraint->position;
    edit.spacing = constraint->spacing;
    edit.spacing_mode = constraint->spacing_mode;
    edit.rotate_mix = constraint->rotate_mix;
    edit.translate_mix = constraint->translate_mix;
    return edit;
}

std::optional<marrow::editor::TransformConstraintEdit> make_transform_constraint_edit_from_runtime(
    const ShellState& state,
    std::string_view name) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto* constraint =
        find_named_constraint(state.load_result.skeleton_data->transform_constraints(), name);
    if (constraint == nullptr) {
        return std::nullopt;
    }

    marrow::editor::TransformConstraintEdit edit;
    edit.name = constraint->name;
    if (constraint->source_bone_index >= state.load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }
    edit.source_bone_name =
        state.load_result.skeleton_data->bones()[constraint->source_bone_index].name;
    for (const std::size_t bone_index : constraint->target_bone_indices) {
        if (bone_index >= state.load_result.skeleton_data->bones().size()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(state.load_result.skeleton_data->bones()[bone_index].name);
    }
    edit.rotate_mix = constraint->rotate_mix;
    edit.translate_mix = constraint->translate_mix;
    edit.scale_mix = constraint->scale_mix;
    edit.shear_mix = constraint->shear_mix;
    edit.offsets = constraint->offsets;
    return edit;
}

std::optional<marrow::editor::PhysicsConstraintEdit> make_physics_constraint_edit_from_runtime(
    const ShellState& state,
    std::string_view name) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto* constraint =
        find_named_constraint(state.load_result.skeleton_data->physics_constraints(), name);
    if (constraint == nullptr) {
        return std::nullopt;
    }

    marrow::editor::PhysicsConstraintEdit edit;
    edit.name = constraint->name;
    for (const std::size_t bone_index : constraint->bone_indices) {
        if (bone_index >= state.load_result.skeleton_data->bones().size()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(state.load_result.skeleton_data->bones()[bone_index].name);
    }
    edit.inertia = constraint->inertia;
    edit.damping = constraint->damping;
    edit.strength = constraint->strength;
    edit.gravity = constraint->gravity;
    edit.wind = constraint->wind;
    edit.mix = constraint->mix;
    return edit;
}

std::optional<std::size_t> ensure_ik_constraint_edit_index(
    ShellState* state,
    std::string_view name) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return std::nullopt;
    }
    const auto existing = std::find_if(
        state->load_result.project->ik_constraint_edits.begin(),
        state->load_result.project->ik_constraint_edits.end(),
        [&](const marrow::editor::IkConstraintEdit& edit) {
            return edit.name == name;
        });
    if (existing != state->load_result.project->ik_constraint_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(state->load_result.project->ik_constraint_edits.begin(), existing));
    }

    const auto edit = make_ik_constraint_edit_from_runtime(*state, name);
    if (!edit.has_value()) {
        return std::nullopt;
    }
    state->load_result.project->ik_constraint_edits.push_back(*edit);
    return state->load_result.project->ik_constraint_edits.size() - 1U;
}

std::optional<std::size_t> ensure_path_constraint_edit_index(
    ShellState* state,
    std::string_view name) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return std::nullopt;
    }
    const auto existing = std::find_if(
        state->load_result.project->path_constraint_edits.begin(),
        state->load_result.project->path_constraint_edits.end(),
        [&](const marrow::editor::PathConstraintEdit& edit) {
            return edit.name == name;
        });
    if (existing != state->load_result.project->path_constraint_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(state->load_result.project->path_constraint_edits.begin(), existing));
    }

    const auto edit = make_path_constraint_edit_from_runtime(*state, name);
    if (!edit.has_value()) {
        return std::nullopt;
    }
    state->load_result.project->path_constraint_edits.push_back(*edit);
    return state->load_result.project->path_constraint_edits.size() - 1U;
}

std::optional<std::size_t> ensure_transform_constraint_edit_index(
    ShellState* state,
    std::string_view name) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return std::nullopt;
    }
    const auto existing = std::find_if(
        state->load_result.project->transform_constraint_edits.begin(),
        state->load_result.project->transform_constraint_edits.end(),
        [&](const marrow::editor::TransformConstraintEdit& edit) {
            return edit.name == name;
        });
    if (existing != state->load_result.project->transform_constraint_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(state->load_result.project->transform_constraint_edits.begin(), existing));
    }

    const auto edit = make_transform_constraint_edit_from_runtime(*state, name);
    if (!edit.has_value()) {
        return std::nullopt;
    }
    state->load_result.project->transform_constraint_edits.push_back(*edit);
    return state->load_result.project->transform_constraint_edits.size() - 1U;
}

std::optional<std::size_t> ensure_physics_constraint_edit_index(
    ShellState* state,
    std::string_view name) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return std::nullopt;
    }
    const auto existing = std::find_if(
        state->load_result.project->physics_constraint_edits.begin(),
        state->load_result.project->physics_constraint_edits.end(),
        [&](const marrow::editor::PhysicsConstraintEdit& edit) {
            return edit.name == name;
        });
    if (existing != state->load_result.project->physics_constraint_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(state->load_result.project->physics_constraint_edits.begin(), existing));
    }

    const auto edit = make_physics_constraint_edit_from_runtime(*state, name);
    if (!edit.has_value()) {
        return std::nullopt;
    }
    state->load_result.project->physics_constraint_edits.push_back(*edit);
    return state->load_result.project->physics_constraint_edits.size() - 1U;
}

std::optional<marrow::editor::IkConstraintEdit> make_default_ik_constraint_edit(
    const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto& skeleton = *state.load_result.skeleton_data;

    marrow::editor::IkConstraintEdit edit;
    edit.name = unique_constraint_name(state, ConstraintEditKind::Ik, "ik_constraint");
    edit.bone_names = preferred_chain(skeleton, {"ik_upper", "ik_lower"})
        .value_or(first_direct_chain(skeleton, 2).value_or(std::vector<std::string>{}));
    if (edit.bone_names.empty()) {
        const auto single_bone = first_non_root_bone_name(skeleton);
        if (!single_bone.has_value()) {
            return std::nullopt;
        }
        edit.bone_names.push_back(*single_bone);
    }
    edit.target_bone_name = named_bone_if_exists(skeleton, "ik_target")
        .value_or(first_constraint_target_name(skeleton, edit.bone_names).value_or(std::string{}));
    if (edit.target_bone_name.empty()) {
        return std::nullopt;
    }
    return edit;
}

std::optional<marrow::editor::PathConstraintEdit> make_default_path_constraint_edit(
    const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto& skeleton = *state.load_result.skeleton_data;

    marrow::editor::PathConstraintEdit edit;
    edit.name = unique_constraint_name(state, ConstraintEditKind::Path, "path_constraint");
    edit.slot_name = named_slot_if_exists(skeleton, "guide")
        .value_or(path_slot_names(skeleton).empty() ? std::string{} : path_slot_names(skeleton).front());
    if (edit.slot_name.empty()) {
        return std::nullopt;
    }
    edit.bone_names = preferred_chain(skeleton, {"path_a", "path_b", "path_c"})
        .value_or(first_direct_chain(skeleton, 3).value_or(std::vector<std::string>{}));
    if (edit.bone_names.empty()) {
        return std::nullopt;
    }
    edit.position = 0.1;
    edit.spacing = 0.3;
    edit.spacing_mode = marrow::runtime::PathConstraintSpacingMode::Percent;
    edit.rotate_mix = 1.0;
    edit.translate_mix = 1.0;
    return edit;
}

std::optional<marrow::editor::TransformConstraintEdit> make_default_transform_constraint_edit(
    const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto& skeleton = *state.load_result.skeleton_data;

    marrow::editor::TransformConstraintEdit edit;
    edit.name =
        unique_constraint_name(state, ConstraintEditKind::Transform, "transform_constraint");
    edit.source_bone_name = named_bone_if_exists(skeleton, "transform_source")
        .value_or(first_non_root_bone_name(skeleton).value_or(std::string{}));
    if (edit.source_bone_name.empty()) {
        return std::nullopt;
    }
    if (const auto preferred_target = named_bone_if_exists(skeleton, "transform_target")) {
        edit.bone_names = {*preferred_target};
    } else if (const auto target_name = first_constraint_target_name(
                   skeleton,
                   std::vector<std::string>{edit.source_bone_name})) {
        edit.bone_names = {*target_name};
    } else {
        return std::nullopt;
    }
    edit.rotate_mix = 0.5;
    edit.translate_mix = 0.25;
    edit.scale_mix = 1.0;
    edit.shear_mix = 0.75;
    edit.offsets.rotation = 15.0;
    edit.offsets.x = -10.0;
    edit.offsets.y = 20.0;
    edit.offsets.scale_x = 0.2;
    edit.offsets.scale_y = -0.1;
    edit.offsets.shear_x = 5.0;
    edit.offsets.shear_y = -2.0;
    return edit;
}

std::optional<marrow::editor::PhysicsConstraintEdit> make_default_physics_constraint_edit(
    const ShellState& state) {
    if (!state.load_result) {
        return std::nullopt;
    }
    const auto& skeleton = *state.load_result.skeleton_data;

    marrow::editor::PhysicsConstraintEdit edit;
    edit.name = unique_constraint_name(state, ConstraintEditKind::Physics, "physics_constraint");
    edit.bone_names = preferred_chain(skeleton, {"ribbon_01", "ribbon_02"})
        .value_or(first_direct_chain(skeleton, 2).value_or(std::vector<std::string>{}));
    if (edit.bone_names.empty()) {
        return std::nullopt;
    }
    edit.inertia = 0.85;
    edit.damping = 4.0;
    edit.strength = 18.0;
    edit.gravity = {0.0, -24.0};
    edit.wind = {12.0, 0.0};
    edit.mix = 1.0;
    return edit;
}

bool draw_string_combo(
    const char* label,
    const std::vector<std::string>& options,
    std::string* value) {
    const char* preview = value->empty() ? "<none>" : value->c_str();
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (const std::string& option : options) {
            const bool selected = *value == option;
            if (ImGui::Selectable(option.c_str(), selected)) {
                *value = option;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

bool set_preview_skin_enabled(
    ShellState* state,
    std::size_t skin_index,
    bool enabled,
    bool update_status_message) {
    if (!state->load_result || skin_index >= state->load_result.skeleton_data->skins().size()) {
        return false;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    if (is_default_skin_index(skeleton, skin_index)) {
        return false;
    }

    const std::string& skin_name = skeleton.skins()[skin_index].name;
    const auto existing = std::find(
        state->preview_skin_names.begin(),
        state->preview_skin_names.end(),
        skin_name);

    if (enabled) {
        if (existing == state->preview_skin_names.end()) {
            state->preview_skin_names.push_back(skin_name);
        }
    } else if (existing != state->preview_skin_names.end()) {
        state->preview_skin_names.erase(existing);
    }

    return apply_preview_skin_selection(state, "Skin Preview", update_status_message);
}

bool apply_attachment_selection_to_preview_slot(
    ShellState* state,
    const AttachmentSelection& selection,
    std::string_view source,
    bool update_status_message) {
    if (!state->load_result || !state->preview_skeleton ||
        selection.slot_index >= state->preview_skeleton->slot_states().size()) {
        return false;
    }

    if (!resolve_attachment_reference(*state->load_result.skeleton_data, selection).has_value()) {
        return false;
    }

    if (selection.slot_index >= state->preview_slot_overrides.size()) {
        state->preview_slot_overrides.resize(state->preview_skeleton->slot_states().size());
    }
    state->preview_slot_overrides[selection.slot_index] = selection;
    select_attachment(state, selection, source, false);
    if (!refresh_preview_pose(state)) {
        return false;
    }

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Preview slot "
               << state->load_result.skeleton_data->slots()[selection.slot_index].name
               << " set to " << selection.attachment_name;
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}

bool reset_preview_slot_to_skin_selection(
    ShellState* state,
    std::size_t slot_index,
    std::string_view source,
    bool update_status_message) {
    if (!state->load_result || !state->preview_skeleton ||
        slot_index >= state->preview_skeleton->slot_states().size()) {
        return false;
    }

    if (slot_index >= state->preview_slot_overrides.size()) {
        state->preview_slot_overrides.resize(state->preview_skeleton->slot_states().size());
    }
    state->preview_slot_overrides[slot_index].reset();
    if (!refresh_preview_pose(state)) {
        return false;
    }

    if (const auto preview_selection = current_attachment_selection(*state, slot_index)) {
        select_attachment(state, preview_selection, source, false);
    } else {
        state->selected_attachment.reset();
    }

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Reset preview slot "
               << state->load_result.skeleton_data->slots()[slot_index].name
               << " to the active skin composition";
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}

void draw_attachment_details(
    const ShellState& state,
    const SlotAttachmentReference& reference) {
    const auto& skeleton = *state.load_result.skeleton_data;
    const auto& slot = skeleton.slots()[reference.slot_index];
    const auto& attachment = *reference.attachment;

    ImGui::Text("Attachment: %s", attachment.name.c_str());
    ImGui::Text("Slot: %s", slot.name.c_str());
    ImGui::Text("Source skin: %s", source_skin_name(skeleton, reference.skin_index).c_str());
    ImGui::Text("Kind: %s", attachment_kind_name(attachment.kind));
    if (!attachment.region_name.empty()) {
        ImGui::Text("Region: %s", attachment.region_name.c_str());
    }

    if (const auto preview_selection = current_attachment_selection(state, reference.slot_index)) {
        ImGui::Text(
            "Preview active: %s",
            yes_no(attachment_matches_selection(*preview_selection, reference)));
    } else {
        ImGui::TextUnformatted("Preview active: no");
    }

    if (attachment.sequence.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Sequence");
        ImGui::Text(
            "Frames: %zu @ %.1f fps",
            attachment.sequence->frame_regions.size(),
            attachment.sequence->fps);
        ImGui::Text(
            "Playback: %s",
            sequence_playback_mode_name(attachment.sequence->playback_mode));
        ImGui::Text("Setup frame: %zu", attachment.sequence->setup_frame);
    }

    if (attachment.mesh_geometry != nullptr) {
        std::size_t weighted_vertices = 0;
        for (const auto& weights : attachment.mesh_geometry->weights) {
            if (!weights.influences.empty()) {
                ++weighted_vertices;
            }
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Mesh Data");
        ImGui::Text("Vertices: %zu", attachment.mesh_geometry->vertices.size() / 2U);
        ImGui::Text("Triangles: %zu", attachment.mesh_geometry->triangles.size() / 3U);
        ImGui::Text(
            "Weighted vertices: %zu / %zu",
            weighted_vertices,
            attachment.mesh_geometry->weights.size());

        const std::vector<MeshWeightVertexRow> weight_rows =
            build_mesh_weight_rows(skeleton, attachment);
        if (!weight_rows.empty() &&
            ImGui::TreeNodeEx("Mesh Weights", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextUnformatted(
                "Vertex-local positions and per-bone bind offsets mirror the exported weighted mesh.");
            ImGui::BeginChild("mesh_weight_rows", ImVec2(0.0f, 180.0f), true);
            for (const MeshWeightVertexRow& row : weight_rows) {
                const std::string header = "Vertex " + std::to_string(row.vertex_index) +
                    "  local(" + std::to_string(row.local_x) + ", " +
                    std::to_string(row.local_y) + ")";
                if (ImGui::TreeNodeEx(
                        header.c_str(),
                        ImGuiTreeNodeFlags_DefaultOpen |
                            (row.influences.empty() ? ImGuiTreeNodeFlags_Leaf : 0))) {
                    if (row.influences.empty()) {
                        ImGui::TextUnformatted("No bone influences.");
                    } else {
                        for (const MeshWeightInfluenceRow& influence : row.influences) {
                            ImGui::BulletText(
                                "%s  bind(%.1f, %.1f)  weight %.3f",
                                influence.bone_name.c_str(),
                                influence.bind_x,
                                influence.bind_y,
                                influence.weight);
                        }
                    }
                    ImGui::TreePop();
                }
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::LinkedMesh &&
        attachment.linked_mesh.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Linked Mesh");
        ImGui::Text(
            "Parent attachment: %s",
            attachment.linked_mesh->parent_attachment.c_str());
        std::string parent_skin = "<current skin>";
        if (attachment.linked_mesh->parent_skin_name.has_value()) {
            parent_skin = *attachment.linked_mesh->parent_skin_name;
        } else if (attachment.linked_mesh->parent_skin_index.has_value()) {
            parent_skin = source_skin_name(skeleton, attachment.linked_mesh->parent_skin_index);
        }
        ImGui::Text("Parent skin: %s", parent_skin.c_str());
        ImGui::Text("Inherit deform: %s", yes_no(attachment.linked_mesh->deform));
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::Point &&
        attachment.point_attachment.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Point");
        ImGui::Text(
            "Local position: (%.1f, %.1f)",
            attachment.point_attachment->local_position.x,
            attachment.point_attachment->local_position.y);
        ImGui::Text("Rotation: %.1f deg", attachment.point_attachment->rotation);
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::BoundingBox &&
        attachment.bounding_box.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Bounding Box");
        ImGui::Text("Vertices: %zu", attachment.bounding_box->polygon.size());
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::Clipping &&
        attachment.clipping_attachment.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Clipping");
        ImGui::Text("Vertices: %zu", attachment.clipping_attachment->polygon.size());
        ImGui::Text("End slot: %s", attachment.clipping_attachment->end_slot_name.c_str());
    }

    if (attachment.kind == marrow::runtime::AttachmentKind::Path &&
        attachment.path_attachment.has_value()) {
        ImGui::Separator();
        ImGui::TextUnformatted("Path");
        ImGui::Text("Control points: %zu", attachment.path_attachment->control_points.size());
    }
}

std::vector<std::vector<std::size_t>> build_bone_children(
    const marrow::runtime::SkeletonData& skeleton) {
    std::vector<std::vector<std::size_t>> children(skeleton.bones().size());
    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        const auto& bone = skeleton.bones()[bone_index];
        if (bone.parent_index.has_value() && *bone.parent_index < children.size()) {
            children[*bone.parent_index].push_back(bone_index);
        }
    }
    return children;
}

std::string parent_bone_name(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::BoneData& bone) {
    if (!bone.parent_index.has_value() || *bone.parent_index >= skeleton.bones().size()) {
        return "<root>";
    }
    return skeleton.bones()[*bone.parent_index].name;
}

std::string join_slots_for_bone(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t bone_index) {
    std::vector<std::string> slot_names;
    for (const auto& slot : skeleton.slots()) {
        if (slot.bone_index == bone_index) {
            slot_names.push_back(slot.name);
        }
    }
    return join_strings(slot_names);
}

const char* yes_no(bool value) {
    return value ? "yes" : "no";
}

std::optional<std::string_view> default_skin_name(
    const marrow::runtime::SkeletonData& skeleton) {
    const auto default_skin_index = skeleton.default_skin_index();
    if (!default_skin_index.has_value() || *default_skin_index >= skeleton.skins().size()) {
        return std::nullopt;
    }

    return skeleton.skins()[*default_skin_index].name;
}

bool is_default_skin_index(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t skin_index) {
    const auto default_skin_index = skeleton.default_skin_index();
    return default_skin_index.has_value() && *default_skin_index == skin_index;
}

std::string source_skin_name(
    const marrow::runtime::SkeletonData& skeleton,
    std::optional<std::size_t> skin_index) {
    if (!skin_index.has_value() || *skin_index >= skeleton.skins().size()) {
        return "<unresolved>";
    }

    return skeleton.skins()[*skin_index].name;
}

const char* blend_mode_name(marrow::runtime::BlendMode blend_mode) {
    switch (blend_mode) {
    case marrow::runtime::BlendMode::Normal:
        return "normal";
    case marrow::runtime::BlendMode::Additive:
        return "additive";
    case marrow::runtime::BlendMode::Multiply:
        return "multiply";
    case marrow::runtime::BlendMode::Screen:
        return "screen";
    }

    return "unknown";
}

const char* attachment_kind_name(marrow::runtime::AttachmentKind kind) {
    switch (kind) {
    case marrow::runtime::AttachmentKind::Region:
        return "region";
    case marrow::runtime::AttachmentKind::Mesh:
        return "mesh";
    case marrow::runtime::AttachmentKind::LinkedMesh:
        return "linked mesh";
    case marrow::runtime::AttachmentKind::Point:
        return "point";
    case marrow::runtime::AttachmentKind::BoundingBox:
        return "bounding box";
    case marrow::runtime::AttachmentKind::Clipping:
        return "clipping";
    case marrow::runtime::AttachmentKind::Path:
        return "path";
    }

    return "unknown";
}

const char* sequence_playback_mode_name(
    marrow::runtime::SequencePlaybackMode mode) {
    switch (mode) {
    case marrow::runtime::SequencePlaybackMode::Hold:
        return "hold";
    case marrow::runtime::SequencePlaybackMode::Once:
        return "once";
    case marrow::runtime::SequencePlaybackMode::Loop:
        return "loop";
    case marrow::runtime::SequencePlaybackMode::PingPong:
        return "ping-pong";
    case marrow::runtime::SequencePlaybackMode::OnceReverse:
        return "once reverse";
    case marrow::runtime::SequencePlaybackMode::LoopReverse:
        return "loop reverse";
    case marrow::runtime::SequencePlaybackMode::PingPongReverse:
        return "ping-pong reverse";
    }

    return "unknown";
}

std::string format_slot_color(const marrow::runtime::SlotColor& color) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(2)
           << "(" << color.r << ", "
           << color.g << ", "
           << color.b << ", "
           << color.a << ")";
    return stream.str();
}

std::vector<std::string> normalize_preview_skin_names(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& skin_names) {
    std::vector<std::string> normalized;
    for (const std::string& skin_name : skin_names) {
        const auto skin_index = skeleton.find_skin_index(skin_name);
        if (!skin_index.has_value() || is_default_skin_index(skeleton, *skin_index)) {
            continue;
        }

        if (std::find(normalized.begin(), normalized.end(), skin_name) == normalized.end()) {
            normalized.push_back(skin_name);
        }
    }

    std::sort(
        normalized.begin(),
        normalized.end(),
        [&skeleton](const std::string& lhs, const std::string& rhs) {
            return skeleton.find_skin_index(lhs).value_or(0) <
                skeleton.find_skin_index(rhs).value_or(0);
        });
    return normalized;
}

std::string preview_skin_summary(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names) {
    std::vector<std::string> labels;
    if (const auto default_name = default_skin_name(skeleton)) {
        labels.push_back(std::string(*default_name));
    }
    labels.insert(labels.end(), preview_skin_names.begin(), preview_skin_names.end());
    return join_strings(labels);
}

std::vector<SlotAttachmentReference> collect_slot_attachments(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index) {
    std::vector<SlotAttachmentReference> attachments;
    for (std::size_t skin_index = 0; skin_index < skeleton.skins().size(); ++skin_index) {
        const auto& skin = skeleton.skins()[skin_index];
        for (const auto& slot_attachment : skin.slot_attachments) {
            if (slot_attachment.slot_index != slot_index) {
                continue;
            }

            attachments.push_back(
                SlotAttachmentReference{slot_index, skin_index, &slot_attachment.attachment});
        }
    }
    return attachments;
}

std::optional<SlotAttachmentReference> resolve_attachment_reference(
    const marrow::runtime::SkeletonData& skeleton,
    const AttachmentSelection& selection) {
    if (selection.slot_index >= skeleton.slots().size()) {
        return std::nullopt;
    }

    if (selection.skin_index.has_value()) {
        const marrow::runtime::AttachmentData* attachment =
            skeleton.find_attachment(*selection.skin_index, selection.slot_index);
        if (attachment != nullptr && attachment->name == selection.attachment_name) {
            return SlotAttachmentReference{
                selection.slot_index,
                selection.skin_index,
                attachment};
        }
    }

    std::optional<std::size_t> source_skin_index = selection.skin_index;
    const marrow::runtime::AttachmentData* attachment = skeleton.find_attachment_source(
        selection.slot_index,
        selection.attachment_name,
        &source_skin_index);
    if (attachment == nullptr) {
        return std::nullopt;
    }

    return SlotAttachmentReference{
        selection.slot_index,
        source_skin_index,
        attachment};
}

std::optional<AttachmentSelection> current_attachment_selection(
    const ShellState& state,
    std::size_t slot_index) {
    if (!state.load_result || !state.preview_skeleton ||
        slot_index >= state.preview_skeleton->slot_states().size()) {
        return std::nullopt;
    }

    const auto& slot_state = state.preview_skeleton->slot_states()[slot_index];
    if (slot_state.attachment_name.empty()) {
        return std::nullopt;
    }

    std::optional<std::size_t> source_skin_index = slot_state.attachment_skin_index;
    state.load_result.skeleton_data->find_attachment_source(
        slot_index,
        slot_state.attachment_name,
        &source_skin_index);
    return AttachmentSelection{
        slot_index,
        source_skin_index,
        slot_state.attachment_name};
}

std::vector<MeshWeightVertexRow> build_mesh_weight_rows(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AttachmentData& attachment) {
    std::vector<MeshWeightVertexRow> rows;
    if (attachment.mesh_geometry == nullptr) {
        return rows;
    }

    const auto& geometry = *attachment.mesh_geometry;
    const std::size_t vertex_count =
        std::min(geometry.vertices.size() / 2U, geometry.weights.size());
    rows.reserve(vertex_count);

    for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        MeshWeightVertexRow row;
        row.vertex_index = vertex_index;
        row.local_x = geometry.vertices[(vertex_index * 2U)];
        row.local_y = geometry.vertices[(vertex_index * 2U) + 1U];
        row.influences.reserve(geometry.weights[vertex_index].influences.size());

        for (const auto& influence : geometry.weights[vertex_index].influences) {
            const std::string bone_name =
                influence.bone_index < skeleton.bones().size()
                ? skeleton.bones()[influence.bone_index].name
                : ("<bone " + std::to_string(influence.bone_index) + ">");
            row.influences.push_back(MeshWeightInfluenceRow{
                bone_name,
                influence.x,
                influence.y,
                influence.weight});
        }

        rows.push_back(std::move(row));
    }

    return rows;
}

std::optional<AttachmentSelection> first_attachment_selection_for_slot(
    const marrow::runtime::SkeletonData& skeleton,
    std::size_t slot_index) {
    const auto attachments = collect_slot_attachments(skeleton, slot_index);
    if (attachments.empty()) {
        return std::nullopt;
    }

    return AttachmentSelection{
        slot_index,
        attachments.front().skin_index,
        attachments.front().attachment->name};
}

std::vector<std::size_t> build_active_preview_skin_indices(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names) {
    std::vector<std::size_t> skin_indices;
    if (const auto default_skin_index = skeleton.default_skin_index()) {
        skin_indices.push_back(*default_skin_index);
    }

    for (const std::string& skin_name : preview_skin_names) {
        const auto skin_index = skeleton.find_skin_index(skin_name);
        if (!skin_index.has_value()) {
            continue;
        }
        if (std::find(skin_indices.begin(), skin_indices.end(), *skin_index) == skin_indices.end()) {
            skin_indices.push_back(*skin_index);
        }
    }

    return skin_indices;
}

std::optional<AttachmentSelection> resolve_skin_preview_attachment(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names,
    std::size_t slot_index) {
    if (slot_index >= skeleton.slots().size()) {
        return std::nullopt;
    }

    const auto& slot = skeleton.slots()[slot_index];
    std::string attachment_name = slot.setup_attachment;
    std::optional<std::size_t> skin_index;
    if (!attachment_name.empty()) {
        skeleton.find_attachment_source(slot_index, attachment_name, &skin_index);
    }

    for (const std::size_t active_skin_index :
         build_active_preview_skin_indices(skeleton, preview_skin_names)) {
        const auto* attachment = skeleton.find_attachment(active_skin_index, slot_index);
        if (attachment == nullptr) {
            continue;
        }

        attachment_name = attachment->name;
        skin_index = active_skin_index;
    }

    if (attachment_name.empty()) {
        return std::nullopt;
    }

    return AttachmentSelection{slot_index, skin_index, attachment_name};
}

bool attachment_matches_selection(
    const AttachmentSelection& selection,
    const SlotAttachmentReference& reference) {
    return selection.slot_index == reference.slot_index &&
        selection.attachment_name == reference.attachment->name &&
        selection.skin_index == reference.skin_index;
}

std::optional<std::size_t> draw_order_position(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t slot_index) {
    const auto& draw_order = skeleton.draw_order();
    const auto it = std::find(draw_order.begin(), draw_order.end(), slot_index);
    if (it == draw_order.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(draw_order.begin(), it));
}

const marrow::runtime::AnimationData* selected_animation(const ShellState& state) {
    if (!state.load_result || state.selected_animation_name.empty()) {
        return nullptr;
    }

    return state.load_result.skeleton_data->find_animation(state.selected_animation_name);
}

double selected_animation_duration(const ShellState& state) {
    const marrow::runtime::AnimationData* animation = selected_animation(state);
    return animation != nullptr ? std::max(animation->duration(), 0.0) : 0.0;
}

const marrow::runtime::AnimationData* queued_preview_animation(const ShellState& state) {
    if (!state.load_result || state.preview_queued_animation_name.empty()) {
        return nullptr;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(state.preview_queued_animation_name);
    if (animation == nullptr || animation->name == state.selected_animation_name) {
        return nullptr;
    }

    return animation;
}

std::string default_queued_preview_animation_name(const ShellState& state) {
    if (!state.load_result) {
        return {};
    }

    for (const auto& animation : state.load_result.skeleton_data->animations()) {
        if (animation.name != state.selected_animation_name) {
            return animation.name;
        }
    }

    return {};
}

void normalize_state_preview_settings(ShellState* state) {
    if (state == nullptr || !state->load_result) {
        return;
    }

    if (state->preview_custom_mix_duration < 0.0) {
        state->preview_custom_mix_duration = 0.0;
    }
    if (state->preview_queue_delay < 0.0) {
        state->preview_queue_delay = 0.0;
    }

    const auto* queued_animation = queued_preview_animation(*state);
    if (queued_animation == nullptr) {
        state->preview_queued_animation_name = default_queued_preview_animation_name(*state);
        if (state->preview_queued_animation_name.empty()) {
            state->preview_queue_enabled = false;
        }
    }
}

double timeline_preview_duration(const ShellState& state) {
    const double primary_duration = selected_animation_duration(state);
    if (!state.preview_queue_enabled) {
        return primary_duration;
    }

    const auto* queued_animation = queued_preview_animation(state);
    if (queued_animation == nullptr) {
        return primary_duration;
    }

    return std::max(0.0, primary_duration) +
        std::max(0.0, state.preview_queue_delay) +
        std::max(0.0, queued_animation->duration());
}

std::string format_time_seconds(double time_seconds) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << time_seconds << "s";
    return stream.str();
}

template <typename Keyframe>
std::vector<double> collect_key_times(const std::vector<Keyframe>& keyframes) {
    std::vector<double> key_times;
    key_times.reserve(keyframes.size());
    for (const Keyframe& keyframe : keyframes) {
        key_times.push_back(keyframe.time);
    }
    return key_times;
}

std::vector<TimelineTrackRow> build_timeline_tracks(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AnimationData& animation) {
    std::vector<TimelineTrackRow> tracks;
    tracks.reserve(
        animation.bone_rotate_timelines.size() +
        animation.bone_inherit_timelines.size() +
        animation.bone_translate_timelines.size() +
        animation.bone_scale_timelines.size() +
        animation.bone_shear_timelines.size() +
        animation.slot_attachment_timelines.size() +
        animation.slot_color_timelines.size() +
        animation.mesh_deform_timelines.size() +
        (animation.draw_order_timeline_data.has_value() ? 1U : 0U) +
        (animation.event_timeline_data.has_value() ? 1U : 0U));

    const auto add_bone_track = [&](std::string_view suffix,
                                    std::size_t bone_index,
                                    const auto& keyframes,
                                    std::optional<marrow::editor::TransformTimelineChannel> transform_channel) {
        if (bone_index >= skeleton.bones().size() || keyframes.empty()) {
            return;
        }

        tracks.push_back(TimelineTrackRow{
            "bone:" + std::to_string(bone_index) + ":" + std::string(suffix),
            "Bone / " + skeleton.bones()[bone_index].name + " / " + std::string(suffix),
            animation.name,
            collect_key_times(keyframes),
            bone_index,
            std::nullopt,
            transform_channel,
            std::nullopt});
    };
    const auto add_slot_track = [&](std::string_view suffix,
                                    std::size_t slot_index,
                                    const auto& keyframes) {
        if (slot_index >= skeleton.slots().size() || keyframes.empty()) {
            return;
        }

        tracks.push_back(TimelineTrackRow{
            "slot:" + std::to_string(slot_index) + ":" + std::string(suffix),
            "Slot / " + skeleton.slots()[slot_index].name + " / " + std::string(suffix),
            animation.name,
            collect_key_times(keyframes),
            std::nullopt,
            slot_index,
            std::nullopt,
            std::nullopt});
    };

    for (const auto& timeline : animation.bone_rotate_timelines) {
        add_bone_track(
            "Rotate",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Rotate);
    }
    for (const auto& timeline : animation.bone_translate_timelines) {
        add_bone_track(
            "Translate",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Translate);
    }
    for (const auto& timeline : animation.bone_scale_timelines) {
        add_bone_track(
            "Scale",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Scale);
    }
    for (const auto& timeline : animation.bone_shear_timelines) {
        add_bone_track(
            "Shear",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Shear);
    }
    for (const auto& timeline : animation.bone_inherit_timelines) {
        add_bone_track("Inherit", timeline.bone_index, timeline.keyframes, std::nullopt);
    }
    for (const auto& timeline : animation.slot_attachment_timelines) {
        add_slot_track("Attachment", timeline.slot_index, timeline.keyframes);
    }
    for (const auto& timeline : animation.slot_color_timelines) {
        add_slot_track("Color", timeline.slot_index, timeline.keyframes);
    }
    for (const auto& timeline : animation.mesh_deform_timelines) {
        if (timeline.slot_index >= skeleton.slots().size() || timeline.keyframes.empty()) {
            continue;
        }

        tracks.push_back(TimelineTrackRow{
            "slot:" + std::to_string(timeline.slot_index) + ":deform:" + timeline.attachment_name,
            "Slot / " + skeleton.slots()[timeline.slot_index].name +
                " / Deform / " + timeline.attachment_name,
            animation.name,
            collect_key_times(timeline.keyframes),
            std::nullopt,
            timeline.slot_index,
            std::nullopt,
            timeline.attachment_name});
    }

    if (animation.draw_order_timeline_data.has_value() &&
        !animation.draw_order_timeline_data->keyframes.empty()) {
        tracks.push_back(TimelineTrackRow{
            "global:draw-order",
            "Global / Draw Order",
            animation.name,
            collect_key_times(animation.draw_order_timeline_data->keyframes),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt});
    }

    if (animation.event_timeline_data.has_value() &&
        !animation.event_timeline_data->keyframes.empty()) {
        tracks.push_back(TimelineTrackRow{
            "global:events",
            "Global / Events",
            animation.name,
            collect_key_times(animation.event_timeline_data->keyframes),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt});
    }

    return tracks;
}

const TimelineTrackRow* selected_timeline_track(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (!state.selected_timeline_track_id.has_value()) {
        return nullptr;
    }

    const auto iterator = std::find_if(
        tracks.begin(),
        tracks.end(),
        [&](const TimelineTrackRow& track) {
            return track.id == *state.selected_timeline_track_id;
        });
    return iterator == tracks.end() ? nullptr : &(*iterator);
}

bool timeline_track_matches_selection(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (state.selected_timeline_track_id.has_value() &&
        *state.selected_timeline_track_id == track.id) {
        return true;
    }

    if (track.slot_index.has_value() &&
        state.selected_slot_index == track.slot_index) {
        return true;
    }

    return track.bone_index.has_value() &&
        state.selected_bone_index == track.bone_index;
}

std::optional<double> adjacent_key_time(
    const std::vector<TimelineTrackRow>& tracks,
    double current_time,
    bool forward) {
    std::optional<double> best_time;

    for (const TimelineTrackRow& track : tracks) {
        for (const double key_time : track.key_times) {
            if (forward) {
                if (key_time <= current_time + 1e-6) {
                    continue;
                }
                if (!best_time.has_value() || key_time < *best_time) {
                    best_time = key_time;
                }
            } else {
                if (key_time >= current_time - 1e-6) {
                    continue;
                }
                if (!best_time.has_value() || key_time > *best_time) {
                    best_time = key_time;
                }
            }
        }
    }

    return best_time;
}

std::optional<double> nearest_key_time(
    const TimelineTrackRow& track,
    double target_time,
    double threshold_time) {
    std::optional<double> best_time;
    double best_distance = threshold_time;

    for (const double key_time : track.key_times) {
        const double distance = std::abs(key_time - target_time);
        if (distance > best_distance) {
            continue;
        }

        best_distance = distance;
        best_time = key_time;
    }

    return best_time;
}

void apply_preview_slot_overrides(ShellState* state) {
    if (!state->load_result || !state->preview_skeleton) {
        return;
    }

    auto& slot_states = state->preview_skeleton->slot_states();
    auto& mesh_deforms = state->preview_skeleton->mesh_deform_states();
    for (std::size_t slot_index = 0;
         slot_index < state->preview_slot_overrides.size() && slot_index < slot_states.size();
         ++slot_index) {
        const auto& override_selection = state->preview_slot_overrides[slot_index];
        if (!override_selection.has_value()) {
            continue;
        }

        if (!resolve_attachment_reference(
                *state->load_result.skeleton_data,
                *override_selection).has_value()) {
            state->preview_slot_overrides[slot_index].reset();
            continue;
        }

        slot_states[slot_index].attachment_name = override_selection->attachment_name;
        slot_states[slot_index].attachment_skin_index = override_selection->skin_index;
        if (slot_index < mesh_deforms.size() &&
            mesh_deforms[slot_index].attachment_name != override_selection->attachment_name) {
            mesh_deforms[slot_index].attachment_name.clear();
            mesh_deforms[slot_index].vertex_offsets.clear();
        }
    }
}

std::string_view transform_channel_label(marrow::editor::TransformTimelineChannel channel) {
    switch (channel) {
    case marrow::editor::TransformTimelineChannel::Rotate:
        return "Rotate";
    case marrow::editor::TransformTimelineChannel::Translate:
        return "Translate";
    case marrow::editor::TransformTimelineChannel::Scale:
        return "Scale";
    case marrow::editor::TransformTimelineChannel::Shear:
        return "Shear";
    }

    return "Rotate";
}

void copy_rotate_timeline_edit(
    const std::vector<marrow::runtime::RotateKeyframe>& source,
    marrow::editor::TransformTimelineEdit* edit) {
    edit->keyframes.clear();
    edit->keyframes.reserve(source.size());
    for (const auto& keyframe : source) {
        marrow::editor::TransformKeyframeEdit copied;
        copied.time = keyframe.time;
        copied.angle = keyframe.angle;
        copied.interpolation = keyframe.interpolation;
        edit->keyframes.push_back(std::move(copied));
    }
}

void copy_vector_timeline_edit(
    const std::vector<marrow::runtime::VectorKeyframe>& source,
    marrow::editor::TransformTimelineEdit* edit) {
    edit->keyframes.clear();
    edit->keyframes.reserve(source.size());
    for (const auto& keyframe : source) {
        marrow::editor::TransformKeyframeEdit copied;
        copied.time = keyframe.time;
        copied.x = keyframe.x;
        copied.y = keyframe.y;
        copied.interpolation = keyframe.interpolation;
        edit->keyframes.push_back(std::move(copied));
    }
}

void copy_deform_timeline_edit(
    const std::vector<marrow::runtime::DeformKeyframe>& source,
    marrow::editor::MeshDeformTimelineEdit* edit) {
    edit->keyframes.clear();
    edit->keyframes.reserve(source.size());
    for (const auto& keyframe : source) {
        marrow::editor::DeformKeyframeEdit copied;
        copied.time = keyframe.time;
        copied.vertex_offsets = keyframe.vertex_offsets;
        copied.interpolation = keyframe.interpolation;
        edit->keyframes.push_back(std::move(copied));
    }
}

std::optional<marrow::editor::TransformTimelineEdit> make_transform_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || !track.transform_channel.has_value() ||
        !track.bone_index.has_value() ||
        *track.bone_index >= state.load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    if (animation == nullptr) {
        return std::nullopt;
    }

    marrow::editor::TransformTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.bone_name = state.load_result.skeleton_data->bones()[*track.bone_index].name;
    edit.channel = *track.transform_channel;

    switch (*track.transform_channel) {
    case marrow::editor::TransformTimelineChannel::Rotate: {
        const auto* timeline = animation->find_rotate_timeline(*track.bone_index);
        if (timeline == nullptr) {
            return std::nullopt;
        }
        copy_rotate_timeline_edit(timeline->keyframes, &edit);
        break;
    }
    case marrow::editor::TransformTimelineChannel::Translate: {
        const auto* timeline = animation->find_translate_timeline(*track.bone_index);
        if (timeline == nullptr) {
            return std::nullopt;
        }
        copy_vector_timeline_edit(timeline->keyframes, &edit);
        break;
    }
    case marrow::editor::TransformTimelineChannel::Scale: {
        const auto* timeline = animation->find_scale_timeline(*track.bone_index);
        if (timeline == nullptr) {
            return std::nullopt;
        }
        copy_vector_timeline_edit(timeline->keyframes, &edit);
        break;
    }
    case marrow::editor::TransformTimelineChannel::Shear: {
        const auto* timeline = animation->find_shear_timeline(*track.bone_index);
        if (timeline == nullptr) {
            return std::nullopt;
        }
        copy_vector_timeline_edit(timeline->keyframes, &edit);
        break;
    }
    }

    return edit;
}

std::optional<marrow::editor::MeshDeformTimelineEdit> make_mesh_deform_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || !track.slot_index.has_value() ||
        !track.deform_attachment_name.has_value() ||
        *track.slot_index >= state.load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    if (animation == nullptr) {
        return std::nullopt;
    }

    const auto* timeline = animation->find_deform_timeline(
        *track.slot_index, *track.deform_attachment_name);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    marrow::editor::MeshDeformTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.slot_name = state.load_result.skeleton_data->slots()[*track.slot_index].name;
    edit.attachment_name = *track.deform_attachment_name;
    copy_deform_timeline_edit(timeline->keyframes, &edit);
    return edit;
}

std::optional<std::size_t> ensure_transform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || !track.transform_channel.has_value() ||
        !track.bone_index.has_value() ||
        *track.bone_index >= state->load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }

    const std::string& bone_name =
        state->load_result.skeleton_data->bones()[*track.bone_index].name;
    const auto existing = std::find_if(
        state->load_result.project->transform_timeline_edits.begin(),
        state->load_result.project->transform_timeline_edits.end(),
        [&](const marrow::editor::TransformTimelineEdit& edit) {
            return edit.animation_name == track.animation_name &&
                edit.bone_name == bone_name &&
                edit.channel == *track.transform_channel;
        });
    if (existing != state->load_result.project->transform_timeline_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(
                state->load_result.project->transform_timeline_edits.begin(),
                existing));
    }

    const auto edit = make_transform_timeline_edit(*state, track);
    if (!edit.has_value()) {
        return std::nullopt;
    }

    state->load_result.project->transform_timeline_edits.push_back(*edit);
    return state->load_result.project->transform_timeline_edits.size() - 1U;
}

std::optional<std::size_t> ensure_mesh_deform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || !track.slot_index.has_value() ||
        !track.deform_attachment_name.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }

    const std::string& slot_name =
        state->load_result.skeleton_data->slots()[*track.slot_index].name;
    const auto existing = std::find_if(
        state->load_result.project->mesh_deform_timeline_edits.begin(),
        state->load_result.project->mesh_deform_timeline_edits.end(),
        [&](const marrow::editor::MeshDeformTimelineEdit& edit) {
            return edit.animation_name == track.animation_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == *track.deform_attachment_name;
        });
    if (existing != state->load_result.project->mesh_deform_timeline_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(
                state->load_result.project->mesh_deform_timeline_edits.begin(),
                existing));
    }

    const auto edit = make_mesh_deform_timeline_edit(*state, track);
    if (!edit.has_value()) {
        return std::nullopt;
    }

    state->load_result.project->mesh_deform_timeline_edits.push_back(*edit);
    return state->load_result.project->mesh_deform_timeline_edits.size() - 1U;
}

std::vector<std::string> slot_names_from_indices(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::size_t>& slot_indices) {
    std::vector<std::string> slot_names;
    slot_names.reserve(slot_indices.size());
    for (const std::size_t slot_index : slot_indices) {
        if (slot_index >= skeleton.slots().size()) {
            return {};
        }
        slot_names.push_back(skeleton.slots()[slot_index].name);
    }
    return slot_names;
}

std::optional<marrow::editor::DrawOrderTimelineEdit> make_draw_order_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || track.id != "global:draw-order") {
        return std::nullopt;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    const auto* timeline =
        animation != nullptr ? animation->find_draw_order_timeline() : nullptr;
    if (timeline == nullptr) {
        return std::nullopt;
    }

    marrow::editor::DrawOrderTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.keyframes.reserve(timeline->keyframes.size());
    for (const auto& keyframe : timeline->keyframes) {
        const std::vector<std::string> slot_names =
            slot_names_from_indices(*state.load_result.skeleton_data, keyframe.slot_indices);
        if (slot_names.size() != keyframe.slot_indices.size()) {
            return std::nullopt;
        }

        marrow::editor::DrawOrderKeyframeEdit copied;
        copied.time = keyframe.time;
        copied.slot_names = slot_names;
        edit.keyframes.push_back(std::move(copied));
    }

    return edit;
}

std::optional<std::size_t> ensure_draw_order_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || track.id != "global:draw-order") {
        return std::nullopt;
    }

    const auto existing = std::find_if(
        state->load_result.project->draw_order_timeline_edits.begin(),
        state->load_result.project->draw_order_timeline_edits.end(),
        [&](const marrow::editor::DrawOrderTimelineEdit& edit) {
            return edit.animation_name == track.animation_name;
        });
    if (existing != state->load_result.project->draw_order_timeline_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(
                state->load_result.project->draw_order_timeline_edits.begin(),
                existing));
    }

    const auto edit = make_draw_order_timeline_edit(*state, track);
    if (!edit.has_value()) {
        return std::nullopt;
    }

    state->load_result.project->draw_order_timeline_edits.push_back(*edit);
    return state->load_result.project->draw_order_timeline_edits.size() - 1U;
}

std::optional<marrow::editor::EventTimelineEdit> make_event_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || track.id != "global:events") {
        return std::nullopt;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    const auto* timeline =
        animation != nullptr ? animation->find_event_timeline() : nullptr;
    if (timeline == nullptr) {
        return std::nullopt;
    }

    marrow::editor::EventTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.keyframes.reserve(timeline->keyframes.size());
    for (const auto& keyframe : timeline->keyframes) {
        if (keyframe.event_index >= state.load_result.skeleton_data->events().size()) {
            return std::nullopt;
        }

        marrow::editor::EventKeyframeEdit copied;
        copied.time = keyframe.time;
        copied.event_name =
            state.load_result.skeleton_data->events()[keyframe.event_index].name;
        copied.int_value = keyframe.int_value;
        copied.float_value = keyframe.float_value;
        copied.string_value = keyframe.string_value;
        copied.audio_path = keyframe.audio_path;
        copied.volume = keyframe.volume;
        copied.balance = keyframe.balance;
        edit.keyframes.push_back(std::move(copied));
    }

    return edit;
}

std::optional<std::size_t> ensure_event_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || track.id != "global:events") {
        return std::nullopt;
    }

    const auto existing = std::find_if(
        state->load_result.project->event_timeline_edits.begin(),
        state->load_result.project->event_timeline_edits.end(),
        [&](const marrow::editor::EventTimelineEdit& edit) {
            return edit.animation_name == track.animation_name;
        });
    if (existing != state->load_result.project->event_timeline_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(
                state->load_result.project->event_timeline_edits.begin(),
                existing));
    }

    const auto edit = make_event_timeline_edit(*state, track);
    if (!edit.has_value()) {
        return std::nullopt;
    }

    state->load_result.project->event_timeline_edits.push_back(*edit);
    return state->load_result.project->event_timeline_edits.size() - 1U;
}

marrow::editor::TransformKeyframeEdit sample_transform_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track) {
    marrow::editor::TransformKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    keyframe.interpolation = marrow::runtime::Interpolation::linear();

    if (!state.preview_skeleton || !track.bone_index.has_value() ||
        *track.bone_index >= state.preview_skeleton->bone_poses().size() ||
        !track.transform_channel.has_value()) {
        return keyframe;
    }

    const auto& pose = state.preview_skeleton->bone_poses()[*track.bone_index].local_pose;
    switch (*track.transform_channel) {
    case marrow::editor::TransformTimelineChannel::Rotate:
        keyframe.angle = pose.rotation;
        break;
    case marrow::editor::TransformTimelineChannel::Translate:
        keyframe.x = pose.x;
        keyframe.y = pose.y;
        break;
    case marrow::editor::TransformTimelineChannel::Scale:
        keyframe.x = pose.scale_x;
        keyframe.y = pose.scale_y;
        break;
    case marrow::editor::TransformTimelineChannel::Shear:
        keyframe.x = pose.shear_x;
        keyframe.y = pose.shear_y;
        break;
    }

    return keyframe;
}

marrow::editor::DrawOrderKeyframeEdit sample_draw_order_keyframe(const ShellState& state) {
    marrow::editor::DrawOrderKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    if (!state.load_result || !state.preview_skeleton) {
        return keyframe;
    }

    keyframe.slot_names = slot_names_from_indices(
        *state.load_result.skeleton_data,
        state.preview_skeleton->draw_order());
    return keyframe;
}

marrow::editor::EventKeyframeEdit sample_event_keyframe(const ShellState& state) {
    marrow::editor::EventKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    if (!state.load_result || state.load_result.skeleton_data->events().empty()) {
        return keyframe;
    }

    keyframe.event_name = state.load_result.skeleton_data->events().front().name;
    return keyframe;
}

marrow::editor::DeformKeyframeEdit sample_deform_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track) {
    marrow::editor::DeformKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    keyframe.interpolation = marrow::runtime::Interpolation::linear();

    if (!state.load_result || !state.preview_skeleton || !track.slot_index.has_value() ||
        !track.deform_attachment_name.has_value()) {
        return keyframe;
    }

    const auto* attachment = state.load_result.skeleton_data->find_attachment_source(
        *track.slot_index, *track.deform_attachment_name);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        return keyframe;
    }

    keyframe.vertex_offsets.assign(attachment->mesh_geometry->vertices.size(), 0.0);
    if (*track.slot_index >= state.preview_skeleton->mesh_deform_states().size()) {
        return keyframe;
    }

    const auto& deform_state =
        state.preview_skeleton->mesh_deform_states()[*track.slot_index];
    if (deform_state.attachment_name == *track.deform_attachment_name &&
        deform_state.vertex_offsets.size() == keyframe.vertex_offsets.size()) {
        keyframe.vertex_offsets = deform_state.vertex_offsets;
    }

    return keyframe;
}

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

template <typename Keyframe>
double clamp_existing_key_time(
    const std::vector<Keyframe>& keyframes,
    std::size_t key_index,
    double desired_time,
    double duration) {
    constexpr double kKeySpacing = 0.001;
    const double minimum_time =
        key_index == 0 ? 0.0 : keyframes[key_index - 1].time + kKeySpacing;
    if (key_index + 1 >= keyframes.size()) {
        return std::max(desired_time, minimum_time);
    }

    const double maximum_time = keyframes[key_index + 1].time - kKeySpacing;
    if (maximum_time < minimum_time) {
        return minimum_time;
    }

    return std::clamp(desired_time, minimum_time, maximum_time);
}

template <typename Keyframe>
double clamp_existing_non_decreasing_key_time(
    const std::vector<Keyframe>& keyframes,
    std::size_t key_index,
    double desired_time) {
    const double minimum_time =
        key_index == 0 ? 0.0 : keyframes[key_index - 1].time;
    if (key_index + 1 >= keyframes.size()) {
        return std::max(desired_time, minimum_time);
    }

    return std::clamp(desired_time, minimum_time, keyframes[key_index + 1].time);
}

bool rebuild_project_runtime(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr ||
        state->load_result.base_skeleton_document == nullptr) {
        return false;
    }

    const auto runtime_result = marrow::editor::build_project_runtime(
        *state->load_result.project,
        *state->load_result.base_skeleton_document);
    if (!runtime_result) {
        state->error_message = runtime_result.error->format();
        return false;
    }

    state->load_result.skeleton_data = runtime_result.skeleton_data;
    state->preview_skeleton =
        std::make_unique<marrow::runtime::Skeleton>(state->load_result.skeleton_data);
    state->animation_state =
        std::make_unique<marrow::runtime::AnimationState>(state->load_result.skeleton_data);
    state->preview_skin_names = normalize_preview_skin_names(
        *state->load_result.skeleton_data,
        state->preview_skin_names);
    state->preview_slot_overrides.resize(state->load_result.skeleton_data->slots().size());

    if (!state->selected_animation_name.empty() &&
        state->load_result.skeleton_data->find_animation(state->selected_animation_name) == nullptr) {
        state->selected_animation_name.clear();
    }
    if (state->selected_animation_name.empty() &&
        !state->load_result.project->editor_metadata.active_animation.empty() &&
        state->load_result.skeleton_data->find_animation(
            state->load_result.project->editor_metadata.active_animation) != nullptr) {
        state->selected_animation_name = state->load_result.project->editor_metadata.active_animation;
    }
    if (state->selected_animation_name.empty() &&
        !state->load_result.skeleton_data->animations().empty()) {
        state->selected_animation_name =
            state->load_result.skeleton_data->animations().front().name;
    }
    normalize_state_preview_settings(state);

    if (state->selected_bone_index.has_value() &&
        *state->selected_bone_index >= state->load_result.skeleton_data->bones().size()) {
        state->selected_bone_index.reset();
    }
    if (state->selected_slot_index.has_value() &&
        *state->selected_slot_index >= state->load_result.skeleton_data->slots().size()) {
        state->selected_slot_index.reset();
        state->selected_attachment.reset();
    }
    validate_selected_constraint(state);

    if (!refresh_preview_pose(state)) {
        return false;
    }
    if (state->selected_slot_index.has_value()) {
        sync_attachment_selection_for_slot(state, *state->selected_slot_index);
    }

    state->error_message.clear();
    return true;
}

bool save_project_file(ShellState* state, bool update_status_message) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const auto save_result =
        marrow::editor::save_project(*state->load_result.project, state->project_path);
    if (!save_result) {
        state->error_message = save_result.error->format();
        state->status_message = "Project save failed";
        return false;
    }

    state->load_result.project = save_result.project;
    state->project_dirty = false;
    state->error_message.clear();
    if (update_status_message) {
        state->status_message = "Saved project to " + state->project_path.string();
    }
    return true;
}

bool export_runtime_assets_file(ShellState* state, bool update_status_message) {
    if (!state->load_result || state->load_result.project == nullptr ||
        state->load_result.base_skeleton_document == nullptr) {
        return false;
    }

    marrow::editor::ProjectExportOptions export_options;
    if (state->export_binary_output) {
        export_options.binary_output_path =
            state->load_result.project->resolved_export_binary_path();
    }

    const auto export_result = marrow::editor::export_runtime_assets(
        *state->load_result.project,
        *state->load_result.base_skeleton_document,
        export_options);
    if (!export_result) {
        state->error_message = export_result.error->format();
        state->status_message = "Runtime export failed";
        return false;
    }

    state->error_message.clear();
    if (update_status_message) {
        std::string message = "Exported runtime assets to " + export_result.path.string();
        if (!export_result.atlas_paths.empty()) {
            message += " with " + std::to_string(export_result.atlas_paths.size()) +
                " atlas file";
            if (export_result.atlas_paths.size() != 1U) {
                message += "s";
            }
        }
        if (export_result.binary_path.has_value()) {
            message += " and " + export_result.binary_path->filename().string();
        }
        state->status_message = std::move(message);
    }
    return true;
}

std::optional<std::size_t> preview_root_bone_index(
    const marrow::runtime::SkeletonData& skeleton) {
    if (const auto root_index = skeleton.find_bone_index("root")) {
        return root_index;
    }

    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        if (!skeleton.bones()[bone_index].parent_index.has_value()) {
            return bone_index;
        }
    }

    if (!skeleton.bones().empty()) {
        return 0U;
    }

    return std::nullopt;
}

bool refresh_preview_pose(ShellState* state) {
    if (!state->load_result || !state->preview_skeleton) {
        return false;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    normalize_state_preview_settings(state);
    state->preview_skin_names =
        normalize_preview_skin_names(skeleton, state->preview_skin_names);

    std::vector<std::string_view> skin_names;
    skin_names.reserve(state->preview_skin_names.size());
    for (const std::string& skin_name : state->preview_skin_names) {
        skin_names.push_back(skin_name);
    }

    if (!state->preview_skeleton->set_skin_composition(skin_names)) {
        state->error_message = "Failed to apply the requested preview skin composition.";
        return false;
    }

    if (!state->animation_state ||
        state->animation_state->data().get() != state->load_result.skeleton_data.get()) {
        state->animation_state =
            std::make_unique<marrow::runtime::AnimationState>(state->load_result.skeleton_data);
    }

    state->preview_root_motion_delta = {};
    state->preview_root_motion_total = {};
    state->preview_events.clear();
    const std::optional<std::size_t> root_bone_index = preview_root_bone_index(skeleton);
    const double preview_duration = timeline_preview_duration(*state);

    if (const marrow::runtime::AnimationData* animation = selected_animation(*state)) {
        state->timeline_time_seconds =
            preview_duration > 0.0 ? std::clamp(state->timeline_time_seconds, 0.0, preview_duration)
                                   : 0.0;

        std::vector<marrow::runtime::AnimationEvent> triggered_events;
        state->animation_state->set_listener(
            [&](marrow::runtime::AnimationState&,
                marrow::runtime::AnimationStateEventType type,
                const std::shared_ptr<marrow::runtime::TrackEntry>&,
                const marrow::runtime::AnimationEvent* event) {
                if (type == marrow::runtime::AnimationStateEventType::Event && event != nullptr) {
                    triggered_events.push_back(*event);
                }
            });

        state->animation_state->clear_tracks();
        const bool primary_loop = state->preview_queue_enabled ? false : state->timeline_loop;
        std::shared_ptr<marrow::runtime::TrackEntry> current =
            state->animation_state->set_animation(0, animation->name, primary_loop, 0.0);
        current->reverse = state->preview_reverse;
        current->alpha = 1.0;

        if (state->preview_queue_enabled) {
            if (const auto* queued_animation = queued_preview_animation(*state)) {
                const std::optional<double> mix_duration =
                    state->preview_use_custom_mix_duration
                    ? std::optional<double>(state->preview_custom_mix_duration)
                    : std::nullopt;
                std::shared_ptr<marrow::runtime::TrackEntry> queued_entry =
                    state->animation_state->add_animation(
                        0,
                        queued_animation->name,
                        false,
                        state->preview_queue_delay,
                        mix_duration);
                queued_entry->reverse = state->preview_reverse;
            }
        }

        constexpr double kPreviewStep = 1.0 / 60.0;
        const auto advance_preview_time = [&](double step) {
            state->animation_state->update(step);
            if (root_bone_index.has_value()) {
                const marrow::runtime::RootMotionDelta delta =
                    state->animation_state->extract_root_motion(0, *root_bone_index);
                state->preview_root_motion_delta = delta;
                state->preview_root_motion_total.x += delta.x;
                state->preview_root_motion_total.y += delta.y;
            }
        };

        double elapsed_time = 0.0;
        while ((elapsed_time + kPreviewStep) < (state->timeline_time_seconds - 1e-9)) {
            advance_preview_time(kPreviewStep);
            elapsed_time += kPreviewStep;
        }
        const double final_step = state->timeline_time_seconds - elapsed_time;
        if (final_step > 1e-9) {
            advance_preview_time(final_step);
        }

        state->preview_skeleton->set_attachment_playback_time(state->timeline_time_seconds);
        state->animation_state->apply(*state->preview_skeleton);
        state->preview_events = std::move(triggered_events);
        state->animation_state->set_listener({});
    } else {
        state->animation_state->clear_tracks();
        state->timeline_time_seconds = 0.0;
        state->preview_skeleton->set_to_setup_pose();
        state->preview_skeleton->set_attachment_playback_time(0.0);
    }

    apply_preview_slot_overrides(state);
    state->error_message.clear();
    return true;
}

bool set_selected_animation(
    ShellState* state,
    std::string_view animation_name,
    std::string_view source,
    bool update_status_message,
    bool reset_time) {
    if (!state->load_result) {
        return false;
    }

    const marrow::runtime::AnimationData* animation =
        state->load_result.skeleton_data->find_animation(animation_name);
    if (animation == nullptr) {
        return false;
    }

    state->selected_animation_name = animation->name;
    state->selected_timeline_track_id.reset();
    normalize_state_preview_settings(state);
    if (reset_time) {
        state->timeline_time_seconds = 0.0;
    } else {
        state->timeline_time_seconds = std::clamp(
            state->timeline_time_seconds,
            0.0,
            timeline_preview_duration(*state));
    }

    if (!refresh_preview_pose(state)) {
        return false;
    }

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Selected animation " << animation->name;
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}

bool scrub_timeline_time(
    ShellState* state,
    double time_seconds,
    std::string_view source,
    bool update_status_message) {
    const double duration = timeline_preview_duration(*state);
    state->timeline_time_seconds =
        duration > 0.0 ? std::clamp(time_seconds, 0.0, duration) : 0.0;
    if (!refresh_preview_pose(state)) {
        return false;
    }

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Scrubbed " << format_time_seconds(state->timeline_time_seconds);
        if (!state->selected_animation_name.empty()) {
            stream << " on " << state->selected_animation_name;
        }
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}

void advance_timeline_playback(ShellState* state, double delta_seconds) {
    if (!state->timeline_playing || delta_seconds <= 0.0) {
        return;
    }

    const double duration = timeline_preview_duration(*state);
    if (duration <= 0.0) {
        state->timeline_playing = false;
        return;
    }

    double next_time = state->timeline_time_seconds + delta_seconds;
    if (state->timeline_loop) {
        next_time = std::fmod(next_time, duration);
        if (next_time < 0.0) {
            next_time += duration;
        }
    } else if (next_time >= duration) {
        next_time = duration;
        state->timeline_playing = false;
    }

    state->timeline_time_seconds = next_time;
    refresh_preview_pose(state);
    if (state->preview_skeleton != nullptr) {
        state->preview_skeleton->update_physics(delta_seconds);
    }
}

bool focus_timeline_track(
    ShellState* state,
    const TimelineTrackRow& track,
    double time_seconds,
    std::string_view source,
    bool update_status_message) {
    state->selected_timeline_track_id = track.id;
    if (track.slot_index.has_value()) {
        select_slot(state, *track.slot_index, source, false);
    } else if (track.bone_index.has_value()) {
        select_bone(state, *track.bone_index, source, false);
    }

    if (!scrub_timeline_time(state, time_seconds, source, false)) {
        return false;
    }

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Focused " << track.label
               << " at " << format_time_seconds(state->timeline_time_seconds);
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}

ImVec2 screen_from_world(
    const ViewportLayout& layout,
    double world_x,
    double world_y) {
    return ImVec2(
        layout.screen_center.x +
            static_cast<float>((world_x - layout.world_center_x) * layout.pixels_per_unit),
        layout.screen_center.y -
            static_cast<float>((world_y - layout.world_center_y) * layout.pixels_per_unit));
}

float squared_distance(const ImVec2& a, const ImVec2& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

float point_segment_distance_squared(
    const ImVec2& point,
    const ImVec2& start,
    const ImVec2& end) {
    const float ab_x = end.x - start.x;
    const float ab_y = end.y - start.y;
    const float ab_length_squared = (ab_x * ab_x) + (ab_y * ab_y);
    if (ab_length_squared <= 1e-6f) {
        return squared_distance(point, start);
    }

    const float ap_x = point.x - start.x;
    const float ap_y = point.y - start.y;
    const float projection = std::clamp(
        ((ap_x * ab_x) + (ap_y * ab_y)) / ab_length_squared,
        0.0f,
        1.0f);
    const ImVec2 closest(start.x + (ab_x * projection), start.y + (ab_y * projection));
    return squared_distance(point, closest);
}

float first_grid_line(float anchor, float minimum, float spacing) {
    const float offset = std::fmod(anchor - minimum, spacing);
    return minimum + (offset < 0.0f ? offset + spacing : offset);
}

std::optional<ViewportLayout> build_viewport_layout(
    const ShellState& state,
    const ImVec2& canvas_origin,
    const ImVec2& canvas_size) {
    if (!state.load_result || !state.preview_skeleton) {
        return std::nullopt;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    const auto& world_transforms = state.preview_skeleton->bone_world_transforms();
    if (world_transforms.size() != skeleton.bones().size() || world_transforms.empty()) {
        return std::nullopt;
    }

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();
    bool has_active_bone = false;

    for (std::size_t bone_index = 0; bone_index < world_transforms.size(); ++bone_index) {
        if (!state.preview_skeleton->is_bone_active(bone_index)) {
            continue;
        }

        has_active_bone = true;
        min_x = std::min(min_x, world_transforms[bone_index].world_x);
        min_y = std::min(min_y, world_transforms[bone_index].world_y);
        max_x = std::max(max_x, world_transforms[bone_index].world_x);
        max_y = std::max(max_y, world_transforms[bone_index].world_y);
    }

    if (!has_active_bone) {
        for (const auto& transform : world_transforms) {
            min_x = std::min(min_x, transform.world_x);
            min_y = std::min(min_y, transform.world_y);
            max_x = std::max(max_x, transform.world_x);
            max_y = std::max(max_y, transform.world_y);
        }
    }

    const double extent_x = std::max(max_x - min_x, 80.0);
    const double extent_y = std::max(max_y - min_y, 80.0);
    const float fit_width = std::max(32.0f, canvas_size.x - 96.0f);
    const float fit_height = std::max(32.0f, canvas_size.y - 96.0f);

    ViewportLayout layout;
    layout.canvas_origin = canvas_origin;
    layout.canvas_size = canvas_size;
    layout.canvas_end = ImVec2(canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y);
    layout.screen_center = ImVec2(
        canvas_origin.x + (canvas_size.x * 0.5f) + static_cast<float>(state.viewport.pan_x),
        canvas_origin.y + (canvas_size.y * 0.5f) + static_cast<float>(state.viewport.pan_y));
    layout.world_center_x = (min_x + max_x) * 0.5;
    layout.world_center_y = (min_y + max_y) * 0.5;
    layout.pixels_per_unit = std::max(
        0.25f,
        std::min(
            fit_width / static_cast<float>(extent_x),
            fit_height / static_cast<float>(extent_y))) *
        static_cast<float>(state.viewport.zoom);
    layout.joint_radius = std::clamp(4.0f + (layout.pixels_per_unit * 0.05f), 4.0f, 10.0f);
    layout.world_origin_screen = screen_from_world(layout, 0.0, 0.0);
    layout.bones.reserve(skeleton.bones().size());

    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        layout.bones.push_back(BoneCanvasNode{
            bone_index,
            skeleton.bones()[bone_index].parent_index,
            screen_from_world(
                layout,
                world_transforms[bone_index].world_x,
                world_transforms[bone_index].world_y),
            state.preview_skeleton->is_bone_active(bone_index)});
    }

    return layout;
}

std::optional<std::size_t> pick_bone_at_position(
    const ViewportLayout& layout,
    const ImVec2& position) {
    std::optional<std::size_t> best_bone;
    float best_distance = std::numeric_limits<float>::max();
    const float joint_threshold = layout.joint_radius + 6.0f;
    const float joint_threshold_squared = joint_threshold * joint_threshold;

    for (const BoneCanvasNode& node : layout.bones) {
        const float distance = squared_distance(position, node.screen_position);
        if (distance <= joint_threshold_squared && distance < best_distance) {
            best_distance = distance;
            best_bone = node.bone_index;
        }
    }

    if (best_bone.has_value()) {
        return best_bone;
    }

    const float segment_threshold = std::max(6.0f, layout.joint_radius);
    const float segment_threshold_squared = segment_threshold * segment_threshold;
    for (const BoneCanvasNode& node : layout.bones) {
        if (!node.parent_index.has_value() || *node.parent_index >= layout.bones.size()) {
            continue;
        }

        const BoneCanvasNode& parent = layout.bones[*node.parent_index];
        const float distance = point_segment_distance_squared(
            position,
            parent.screen_position,
            node.screen_position);
        if (distance <= segment_threshold_squared && distance < best_distance) {
            best_distance = distance;
            best_bone = node.bone_index;
        }
    }

    return best_bone;
}

bool reload_project(ShellState* state) {
    std::optional<std::string> previous_selection_name;
    if (const auto selection_name = selected_bone_name(*state)) {
        previous_selection_name = std::string(*selection_name);
    }
    std::optional<std::string> previous_slot_name;
    if (state->load_result && state->selected_slot_index.has_value() &&
        *state->selected_slot_index < state->load_result.skeleton_data->slots().size()) {
        previous_slot_name =
            state->load_result.skeleton_data->slots()[*state->selected_slot_index].name;
    }
    const std::string previous_animation_name = state->selected_animation_name;
    const double previous_timeline_time = state->timeline_time_seconds;
    const bool previous_timeline_loop = state->timeline_loop;
    const bool previous_timeline_playing = state->timeline_playing;

    state->load_result = marrow::editor::load_project(state->project_path);
    state->preview_skeleton.reset();
    state->animation_state.reset();
    state->selected_bone_index.reset();
    state->selected_slot_index.reset();
    state->selected_attachment.reset();
    state->selected_timeline_track_id.reset();
    state->selected_constraint.reset();
    state->preview_skin_names.clear();
    state->preview_slot_overrides.clear();
    state->selected_animation_name.clear();
    state->timeline_time_seconds = 0.0;
    state->timeline_loop = previous_timeline_loop;
    state->timeline_playing = false;
    state->project_dirty = false;
    state->error_message.clear();

    if (!state->load_result) {
        state->status_message = "Project load failed";
        if (state->load_result.error.has_value()) {
            state->error_message = state->load_result.error->format();
        } else {
            state->error_message = "Unknown project load failure.";
        }
        return false;
    }

    state->viewport = state->load_result.project->editor_metadata.viewport;
    state->preview_skeleton =
        std::make_unique<marrow::runtime::Skeleton>(state->load_result.skeleton_data);
    state->animation_state =
        std::make_unique<marrow::runtime::AnimationState>(state->load_result.skeleton_data);
    state->preview_skin_names = normalize_preview_skin_names(
        *state->load_result.skeleton_data,
        state->load_result.project->editor_metadata.preview_skins);
    state->preview_slot_overrides.resize(state->load_result.skeleton_data->slots().size());

    const auto& animations = state->load_result.skeleton_data->animations();
    if (!previous_animation_name.empty() &&
        state->load_result.skeleton_data->find_animation(previous_animation_name) != nullptr) {
        state->selected_animation_name = previous_animation_name;
    } else if (!state->load_result.project->editor_metadata.active_animation.empty() &&
               state->load_result.skeleton_data->find_animation(
                   state->load_result.project->editor_metadata.active_animation) != nullptr) {
        state->selected_animation_name = state->load_result.project->editor_metadata.active_animation;
    } else if (!animations.empty()) {
        state->selected_animation_name = animations.front().name;
    }
    normalize_state_preview_settings(state);
    if (!state->selected_animation_name.empty()) {
        state->timeline_time_seconds = std::clamp(
            previous_timeline_time,
            0.0,
            timeline_preview_duration(*state));
        state->timeline_playing = previous_timeline_playing;
    }
    if (!refresh_preview_pose(state)) {
        state->status_message = "Project load failed";
        return false;
    }

    if (previous_selection_name.has_value()) {
        state->selected_bone_index =
            state->load_result.skeleton_data->find_bone_index(*previous_selection_name);
    }
    if (!state->selected_bone_index.has_value() &&
        !state->load_result.skeleton_data->bones().empty()) {
        state->selected_bone_index = 0;
    }
    if (previous_slot_name.has_value()) {
        state->selected_slot_index =
            state->load_result.skeleton_data->find_slot_index(*previous_slot_name);
    }
    if (!state->selected_slot_index.has_value() &&
        !state->load_result.skeleton_data->slots().empty()) {
        state->selected_slot_index = 0;
    }
    if (state->selected_slot_index.has_value()) {
        sync_attachment_selection_for_slot(state, *state->selected_slot_index);
    }
    validate_selected_constraint(state);

    std::ostringstream stream;
    stream << "Loaded "
           << state->load_result.project->editor_metadata.name
           << " from "
           << state->project_path.string();
    state->status_message = stream.str();
    return true;
}

void draw_menu_bar(GLFWwindow* window, bool* reload_requested, ShellState* state) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Reload Project")) {
            *reload_requested = true;
        }
        if (ImGui::MenuItem("Quit")) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(state->status_message.c_str());
    ImGui::EndMainMenuBar();
}

void draw_project_window(bool* reload_requested, ShellState* state) {
    ImGui::Begin("Project");

    if (ImGui::Button("Reload")) {
        *reload_requested = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("Save Project")) {
        save_project_file(state, true);
    }

    ImGui::SameLine();
    if (ImGui::Button("Export Runtime Assets")) {
        export_runtime_assets_file(state, true);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Export .mbin", &state->export_binary_output);

    ImGui::SameLine();
    ImGui::TextUnformatted(state->project_path.string().c_str());
    ImGui::Separator();

    if (!state->load_result) {
        ImGui::TextUnformatted("Project data is unavailable.");
        if (!state->error_message.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", state->error_message.c_str());
        }
        ImGui::End();
        return;
    }

    const auto& project = *state->load_result.project;
    const auto& skeleton = *state->load_result.skeleton_data;
    const std::string active_animation_label =
        state->selected_animation_name.empty() ? std::string("<setup pose>")
                                               : state->selected_animation_name;
    ImGui::Text("Name: %s", project.editor_metadata.name.c_str());
    ImGui::Text("Animation: %s", active_animation_label.c_str());
    ImGui::Text(
        "Playhead: %s / %s (%s)",
        format_time_seconds(state->timeline_time_seconds).c_str(),
        format_time_seconds(timeline_preview_duration(*state)).c_str(),
        state->timeline_playing ? "playing" : "paused");
    ImGui::Text(
        "Preview mode: %s%s",
        state->preview_queue_enabled ? "queued transition" : "single clip",
        state->preview_reverse ? " / reverse" : "");
    ImGui::Text(
        "Authored default animation: %s",
        project.editor_metadata.active_animation.c_str());
    ImGui::Text(
        "Preview skins: %s",
        preview_skin_summary(skeleton, state->preview_skin_names).c_str());
    ImGui::Text(
        "Edited transform tracks: %zu  Deform: %zu  Draw order: %zu  Events: %zu (%s)",
        project.transform_timeline_edits.size(),
        project.mesh_deform_timeline_edits.size(),
        project.draw_order_timeline_edits.size(),
        project.event_timeline_edits.size(),
        state->project_dirty ? "unsaved changes" : "saved");
    ImGui::Text(
        "Preview root motion: recent(%.2f, %.2f) total(%.2f, %.2f)",
        state->preview_root_motion_delta.x,
        state->preview_root_motion_delta.y,
        state->preview_root_motion_total.x,
        state->preview_root_motion_total.y);
    ImGui::Text(
        "Edited constraints: IK %zu, Path %zu, Transform %zu, Physics %zu (%s)",
        project.ik_constraint_edits.size(),
        project.path_constraint_edits.size(),
        project.transform_constraint_edits.size(),
        project.physics_constraint_edits.size(),
        state->project_dirty ? "unsaved changes" : "saved");
    ImGui::Text("Runtime skeleton: %s", project.resolved_skeleton_path().string().c_str());
    ImGui::Text("Runtime atlases: %s", join_paths(project.resolved_atlas_paths()).c_str());
    ImGui::Text(
        "Export bundle: %s",
        project.resolved_export_skeleton_path().string().c_str());
    if (state->export_binary_output) {
        ImGui::Text(
            "Binary target: %s",
            project.resolved_export_binary_path().string().c_str());
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s", project.editor_metadata.notes.c_str());
    if (!state->error_message.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", state->error_message.c_str());
    }

    ImGui::End();
}

void draw_runtime_window(const ShellState& state) {
    ImGui::Begin("Runtime Assets");

    if (!state.load_result) {
        ImGui::TextUnformatted("Load a valid project to inspect runtime assets.");
        ImGui::End();
        return;
    }

    const marrow::runtime::SkeletonData& skeleton = *state.load_result.skeleton_data;
    const auto& info = skeleton.info();
    ImGui::Text("Skeleton: %s", info.name.c_str());
    ImGui::Text("Bounds: %.0f x %.0f", info.width, info.height);
    ImGui::Text("Bones: %zu", skeleton.bones().size());
    ImGui::Text("Slots: %zu", skeleton.slots().size());
    ImGui::Text("Skins: %zu", skeleton.skins().size());
    ImGui::Text("Animations: %zu", skeleton.animations().size());
    ImGui::Text(
        "Constraints: IK %zu, Path %zu, Transform %zu, Physics %zu",
        skeleton.ik_constraints().size(),
        skeleton.path_constraints().size(),
        skeleton.transform_constraints().size(),
        skeleton.physics_constraints().size());

    if (ImGui::CollapsingHeader("Animation Clips", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& animation : skeleton.animations()) {
            ImGui::BulletText("%s (%.2fs)", animation.name.c_str(), animation.duration());
        }
    }

    if (ImGui::CollapsingHeader("Skins", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& skin : skeleton.skins()) {
            ImGui::BulletText("%s", skin.name.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Constraints", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& constraint : skeleton.ik_constraints()) {
            ImGui::BulletText("IK: %s", constraint.name.c_str());
        }
        for (const auto& constraint : skeleton.path_constraints()) {
            ImGui::BulletText("Path: %s", constraint.name.c_str());
        }
        for (const auto& constraint : skeleton.transform_constraints()) {
            ImGui::BulletText("Transform: %s", constraint.name.c_str());
        }
        for (const auto& constraint : skeleton.physics_constraints()) {
            ImGui::BulletText("Physics: %s", constraint.name.c_str());
        }
    }

    if (ImGui::CollapsingHeader("Atlases", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& atlas : state.load_result.atlas_data) {
            ImGui::BulletText(
                "%s (%zu regions, %.0f x %.0f)",
                atlas->info().name.c_str(),
                atlas->regions().size(),
                atlas->info().width,
                atlas->info().height);
        }
    }

    ImGui::End();
}

void draw_constraints_window(ShellState* state) {
    ImGui::Begin("Constraints");

    if (!state->load_result || state->load_result.project == nullptr) {
        ImGui::TextUnformatted("Load a valid project to author constraint overrides.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    auto* project = state->load_result.project.get();
    const std::vector<std::string> bone_options = all_bone_names(skeleton);
    const std::vector<std::string> path_slots = path_slot_names(skeleton);
    constexpr double kZero = 0.0;
    constexpr double kOne = 1.0;
    constexpr double kTen = 10.0;

    validate_selected_constraint(state);

    const auto commit_constraint_change = [&](const marrow::editor::ProjectData& previous_project,
                                              ConstraintEditKind kind,
                                              std::string_view name,
                                              std::string failure_message,
                                              std::string success_message) {
        if (!rebuild_project_runtime(state)) {
            const std::string rebuild_error = state->error_message;
            *state->load_result.project = previous_project;
            rebuild_project_runtime(state);
            state->error_message = rebuild_error;
            state->status_message = std::move(failure_message);
            return false;
        }

        state->project_dirty = true;
        select_constraint(state, kind, name, "", false);
        state->status_message = std::move(success_message);
        return true;
    };

    const auto append_direct_child_name = [&](std::vector<std::string>* bone_names) {
        if (bone_names == nullptr || bone_names->empty()) {
            return false;
        }
        const auto last_bone_index = skeleton.find_bone_index(bone_names->back());
        if (!last_bone_index.has_value()) {
            return false;
        }
        const auto child_index = first_child_bone_index(skeleton, *last_bone_index);
        if (!child_index.has_value()) {
            return false;
        }
        const std::string& child_name = skeleton.bones()[*child_index].name;
        if (std::find(bone_names->begin(), bone_names->end(), child_name) != bone_names->end()) {
            return false;
        }
        bone_names->push_back(child_name);
        return true;
    };

    if (ImGui::BeginTabBar("constraint_tabs")) {
        if (ImGui::BeginTabItem("IK")) {
            if (ImGui::Button("Add IK Constraint")) {
                const marrow::editor::ProjectData previous_project = *project;
                if (const auto new_edit = make_default_ik_constraint_edit(*state)) {
                    project->ik_constraint_edits.push_back(*new_edit);
                    commit_constraint_change(
                        previous_project,
                        ConstraintEditKind::Ik,
                        new_edit->name,
                        "IK constraint edit failed",
                        "Added IK constraint " + new_edit->name);
                } else {
                    state->status_message = "Could not derive a valid default IK constraint";
                }
            }

            ImGui::BeginChild("ik_constraint_list", ImVec2(0.0f, 120.0f), true);
            for (const auto& constraint : skeleton.ik_constraints()) {
                const bool selected =
                    state->selected_constraint.has_value() &&
                    state->selected_constraint->kind == ConstraintEditKind::Ik &&
                    state->selected_constraint->name == constraint.name;
                const bool has_project_edit =
                    project->find_ik_constraint_edit(constraint.name) != nullptr;
                const std::string label =
                    constraint.name + (has_project_edit ? "  [project]" : "  [runtime]");
                if (ImGui::Selectable(label.c_str(), selected)) {
                    select_constraint(state, ConstraintEditKind::Ik, constraint.name, "Constraints", true);
                }
            }
            ImGui::EndChild();

            const std::string selected_name =
                state->selected_constraint.has_value() &&
                    state->selected_constraint->kind == ConstraintEditKind::Ik &&
                    find_named_constraint(skeleton.ik_constraints(), state->selected_constraint->name) != nullptr
                ? state->selected_constraint->name
                : (!skeleton.ik_constraints().empty() ? skeleton.ik_constraints().front().name : std::string{});
            if (selected_name.empty()) {
                ImGui::TextUnformatted("No IK constraints are active in the current runtime preview.");
            } else {
                const auto runtime_edit = make_ik_constraint_edit_from_runtime(*state, selected_name);
                const marrow::editor::IkConstraintEdit* project_edit =
                    project->find_ik_constraint_edit(selected_name);
                const marrow::editor::IkConstraintEdit display_edit =
                    project_edit != nullptr ? *project_edit : *runtime_edit;

                ImGui::Separator();
                ImGui::Text("Name: %s", display_edit.name.c_str());
                ImGui::Text(
                    "Source: %s",
                    project_edit != nullptr ? "project constraint edit" : "runtime constraint");

                int chain_length = static_cast<int>(display_edit.bone_names.size());
                if (ImGui::RadioButton("1 Bone", chain_length == 1)) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_ik_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->ik_constraint_edits[*edit_index];
                        if (edit.bone_names.size() > 1U) {
                            edit.bone_names.resize(1U);
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Ik,
                                edit.name,
                                "IK constraint edit failed",
                                "Updated IK chain length on " + edit.name);
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::RadioButton("2 Bones", chain_length == 2)) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_ik_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->ik_constraint_edits[*edit_index];
                        if (edit.bone_names.size() < 2U) {
                            if (!append_direct_child_name(&edit.bone_names)) {
                                if (const auto chain = preferred_chain(skeleton, {"ik_upper", "ik_lower"})) {
                                    edit.bone_names = *chain;
                                }
                            }
                            if (edit.bone_names.size() >= 2U) {
                                commit_constraint_change(
                                    previous_project,
                                    ConstraintEditKind::Ik,
                                    edit.name,
                                    "IK constraint edit failed",
                                    "Updated IK chain length on " + edit.name);
                            } else {
                                *project = previous_project;
                                state->status_message =
                                    "Could not extend the IK chain to a valid two-bone setup";
                            }
                        }
                    }
                }

                for (std::size_t bone_index = 0; bone_index < display_edit.bone_names.size(); ++bone_index) {
                    std::string edited_bone = display_edit.bone_names[bone_index];
                    const std::string label = "Bone " + std::to_string(bone_index + 1U);
                    if (draw_string_combo(label.c_str(), bone_options, &edited_bone)) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index = ensure_ik_constraint_edit_index(state, selected_name)) {
                            project->ik_constraint_edits[*edit_index].bone_names[bone_index] = edited_bone;
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Ik,
                                project->ik_constraint_edits[*edit_index].name,
                                "IK constraint edit failed",
                                "Updated IK bone selection on " + selected_name);
                        }
                    }
                }

                std::string edited_target = display_edit.target_bone_name;
                if (draw_string_combo("Target", bone_options, &edited_target)) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_ik_constraint_edit_index(state, selected_name)) {
                        project->ik_constraint_edits[*edit_index].target_bone_name = edited_target;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Ik,
                            project->ik_constraint_edits[*edit_index].name,
                            "IK constraint edit failed",
                            "Updated IK target on " + selected_name);
                    }
                }

                double edited_mix = display_edit.mix;
                if (ImGui::SliderScalar(
                        "Mix",
                        ImGuiDataType_Double,
                        &edited_mix,
                        &kZero,
                        &kOne,
                        "%.2f")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_ik_constraint_edit_index(state, selected_name)) {
                        project->ik_constraint_edits[*edit_index].mix = edited_mix;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Ik,
                            project->ik_constraint_edits[*edit_index].name,
                            "IK constraint edit failed",
                            "Updated IK mix on " + selected_name);
                    }
                }

                bool bend_positive = display_edit.bend_positive;
                if (ImGui::Checkbox("Bend Positive", &bend_positive)) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_ik_constraint_edit_index(state, selected_name)) {
                        project->ik_constraint_edits[*edit_index].bend_positive = bend_positive;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Ik,
                            project->ik_constraint_edits[*edit_index].name,
                            "IK constraint edit failed",
                            "Updated IK bend direction on " + selected_name);
                    }
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Path")) {
            if (ImGui::Button("Add Path Constraint")) {
                const marrow::editor::ProjectData previous_project = *project;
                if (const auto new_edit = make_default_path_constraint_edit(*state)) {
                    project->path_constraint_edits.push_back(*new_edit);
                    commit_constraint_change(
                        previous_project,
                        ConstraintEditKind::Path,
                        new_edit->name,
                        "Path constraint edit failed",
                        "Added path constraint " + new_edit->name);
                } else {
                    state->status_message = "Could not derive a valid default path constraint";
                }
            }

            ImGui::BeginChild("path_constraint_list", ImVec2(0.0f, 120.0f), true);
            for (const auto& constraint : skeleton.path_constraints()) {
                const bool selected =
                    state->selected_constraint.has_value() &&
                    state->selected_constraint->kind == ConstraintEditKind::Path &&
                    state->selected_constraint->name == constraint.name;
                const bool has_project_edit =
                    project->find_path_constraint_edit(constraint.name) != nullptr;
                const std::string label =
                    constraint.name + (has_project_edit ? "  [project]" : "  [runtime]");
                if (ImGui::Selectable(label.c_str(), selected)) {
                    select_constraint(
                        state,
                        ConstraintEditKind::Path,
                        constraint.name,
                        "Constraints",
                        true);
                }
            }
            ImGui::EndChild();

            const std::string selected_name =
                state->selected_constraint.has_value() &&
                    state->selected_constraint->kind == ConstraintEditKind::Path &&
                    find_named_constraint(skeleton.path_constraints(), state->selected_constraint->name) != nullptr
                ? state->selected_constraint->name
                : (!skeleton.path_constraints().empty() ? skeleton.path_constraints().front().name : std::string{});
            if (selected_name.empty()) {
                ImGui::TextUnformatted("No path constraints are active in the current runtime preview.");
            } else {
                const auto runtime_edit = make_path_constraint_edit_from_runtime(*state, selected_name);
                const marrow::editor::PathConstraintEdit* project_edit =
                    project->find_path_constraint_edit(selected_name);
                const marrow::editor::PathConstraintEdit display_edit =
                    project_edit != nullptr ? *project_edit : *runtime_edit;

                ImGui::Separator();
                ImGui::Text("Name: %s", display_edit.name.c_str());
                ImGui::Text(
                    "Source: %s",
                    project_edit != nullptr ? "project constraint edit" : "runtime constraint");

                std::string edited_slot = display_edit.slot_name;
                if (draw_string_combo("Guide Slot", path_slots, &edited_slot)) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        project->path_constraint_edits[*edit_index].slot_name = edited_slot;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Path,
                            project->path_constraint_edits[*edit_index].name,
                            "Path constraint edit failed",
                            "Updated guide slot on " + selected_name);
                    }
                }

                if (ImGui::Button("Add Chain Bone")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->path_constraint_edits[*edit_index];
                        if (append_direct_child_name(&edit.bone_names)) {
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Path,
                                edit.name,
                                "Path constraint edit failed",
                                "Extended the path chain on " + selected_name);
                        } else {
                            *project = previous_project;
                            state->status_message =
                                "Could not extend the path chain with a direct child bone";
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Chain Bone")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->path_constraint_edits[*edit_index];
                        if (edit.bone_names.size() > 1U) {
                            edit.bone_names.pop_back();
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Path,
                                edit.name,
                                "Path constraint edit failed",
                                "Shortened the path chain on " + selected_name);
                        }
                    }
                }

                for (std::size_t bone_index = 0; bone_index < display_edit.bone_names.size(); ++bone_index) {
                    std::string edited_bone = display_edit.bone_names[bone_index];
                    const std::string label = "Chain Bone " + std::to_string(bone_index + 1U);
                    if (draw_string_combo(label.c_str(), bone_options, &edited_bone)) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].bone_names[bone_index] =
                                edited_bone;
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Path,
                                project->path_constraint_edits[*edit_index].name,
                                "Path constraint edit failed",
                                "Updated path chain selection on " + selected_name);
                        }
                    }
                }

                double edited_position = display_edit.position;
                if (ImGui::SliderScalar(
                        "Position",
                        ImGuiDataType_Double,
                        &edited_position,
                        &kZero,
                        &kOne,
                        "%.2f")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        project->path_constraint_edits[*edit_index].position = edited_position;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Path,
                            project->path_constraint_edits[*edit_index].name,
                            "Path constraint edit failed",
                            "Updated path position on " + selected_name);
                    }
                }

                double edited_spacing = display_edit.spacing;
                if (ImGui::SliderScalar(
                        "Spacing",
                        ImGuiDataType_Double,
                        &edited_spacing,
                        &kZero,
                        &kOne,
                        "%.2f")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        project->path_constraint_edits[*edit_index].spacing = edited_spacing;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Path,
                            project->path_constraint_edits[*edit_index].name,
                            "Path constraint edit failed",
                            "Updated path spacing on " + selected_name);
                    }
                }

                int spacing_mode = display_edit.spacing_mode ==
                        marrow::runtime::PathConstraintSpacingMode::Percent
                    ? 1
                    : 0;
                constexpr const char* kSpacingModes[] = {"Length", "Percent"};
                if (ImGui::Combo(
                        "Spacing Mode",
                        &spacing_mode,
                        kSpacingModes,
                        IM_ARRAYSIZE(kSpacingModes))) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        project->path_constraint_edits[*edit_index].spacing_mode =
                            spacing_mode == 1 ? marrow::runtime::PathConstraintSpacingMode::Percent
                                              : marrow::runtime::PathConstraintSpacingMode::Length;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Path,
                            project->path_constraint_edits[*edit_index].name,
                            "Path constraint edit failed",
                            "Updated path spacing mode on " + selected_name);
                    }
                }

                double edited_rotate_mix = display_edit.rotate_mix;
                if (ImGui::SliderScalar(
                        "Rotate Mix",
                        ImGuiDataType_Double,
                        &edited_rotate_mix,
                        &kZero,
                        &kOne,
                        "%.2f")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        project->path_constraint_edits[*edit_index].rotate_mix = edited_rotate_mix;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Path,
                            project->path_constraint_edits[*edit_index].name,
                            "Path constraint edit failed",
                            "Updated path rotate mix on " + selected_name);
                    }
                }

                double edited_translate_mix = display_edit.translate_mix;
                if (ImGui::SliderScalar(
                        "Translate Mix",
                        ImGuiDataType_Double,
                        &edited_translate_mix,
                        &kZero,
                        &kOne,
                        "%.2f")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_path_constraint_edit_index(state, selected_name)) {
                        project->path_constraint_edits[*edit_index].translate_mix =
                            edited_translate_mix;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Path,
                            project->path_constraint_edits[*edit_index].name,
                            "Path constraint edit failed",
                            "Updated path translate mix on " + selected_name);
                    }
                }
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Transform")) {
            if (ImGui::Button("Add Transform Constraint")) {
                const marrow::editor::ProjectData previous_project = *project;
                if (const auto new_edit = make_default_transform_constraint_edit(*state)) {
                    project->transform_constraint_edits.push_back(*new_edit);
                    commit_constraint_change(
                        previous_project,
                        ConstraintEditKind::Transform,
                        new_edit->name,
                        "Transform constraint edit failed",
                        "Added transform constraint " + new_edit->name);
                } else {
                    state->status_message = "Could not derive a valid default transform constraint";
                }
            }

            ImGui::BeginChild("transform_constraint_list", ImVec2(0.0f, 120.0f), true);
            for (const auto& constraint : skeleton.transform_constraints()) {
                const bool selected =
                    state->selected_constraint.has_value() &&
                    state->selected_constraint->kind == ConstraintEditKind::Transform &&
                    state->selected_constraint->name == constraint.name;
                const bool has_project_edit =
                    project->find_transform_constraint_edit(constraint.name) != nullptr;
                const std::string label =
                    constraint.name + (has_project_edit ? "  [project]" : "  [runtime]");
                if (ImGui::Selectable(label.c_str(), selected)) {
                    select_constraint(
                        state,
                        ConstraintEditKind::Transform,
                        constraint.name,
                        "Constraints",
                        true);
                }
            }
            ImGui::EndChild();

            const std::string selected_name =
                state->selected_constraint.has_value() &&
                    state->selected_constraint->kind == ConstraintEditKind::Transform &&
                    find_named_constraint(
                        skeleton.transform_constraints(),
                        state->selected_constraint->name) != nullptr
                ? state->selected_constraint->name
                : (!skeleton.transform_constraints().empty()
                       ? skeleton.transform_constraints().front().name
                       : std::string{});
            if (selected_name.empty()) {
                ImGui::TextUnformatted(
                    "No transform constraints are active in the current runtime preview.");
            } else {
                const auto runtime_edit =
                    make_transform_constraint_edit_from_runtime(*state, selected_name);
                const marrow::editor::TransformConstraintEdit* project_edit =
                    project->find_transform_constraint_edit(selected_name);
                const marrow::editor::TransformConstraintEdit display_edit =
                    project_edit != nullptr ? *project_edit : *runtime_edit;

                ImGui::Separator();
                ImGui::Text("Name: %s", display_edit.name.c_str());
                ImGui::Text(
                    "Source: %s",
                    project_edit != nullptr ? "project constraint edit" : "runtime constraint");

                std::string edited_source = display_edit.source_bone_name;
                if (draw_string_combo("Source Bone", bone_options, &edited_source)) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index =
                            ensure_transform_constraint_edit_index(state, selected_name)) {
                        project->transform_constraint_edits[*edit_index].source_bone_name =
                            edited_source;
                        commit_constraint_change(
                            previous_project,
                            ConstraintEditKind::Transform,
                            project->transform_constraint_edits[*edit_index].name,
                            "Transform constraint edit failed",
                            "Updated transform source on " + selected_name);
                    }
                }

                if (ImGui::Button("Add Target Bone")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index =
                            ensure_transform_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->transform_constraint_edits[*edit_index];
                        const auto candidate = first_constraint_target_name(
                            skeleton,
                            [&]() {
                                std::vector<std::string> excluded = edit.bone_names;
                                excluded.push_back(edit.source_bone_name);
                                return excluded;
                            }());
                        if (candidate.has_value()) {
                            edit.bone_names.push_back(*candidate);
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Transform,
                                edit.name,
                                "Transform constraint edit failed",
                                "Added a transform target on " + selected_name);
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Target Bone")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index =
                            ensure_transform_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->transform_constraint_edits[*edit_index];
                        if (edit.bone_names.size() > 1U) {
                            edit.bone_names.pop_back();
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Transform,
                                edit.name,
                                "Transform constraint edit failed",
                                "Removed a transform target on " + selected_name);
                        }
                    }
                }

                for (std::size_t bone_index = 0; bone_index < display_edit.bone_names.size(); ++bone_index) {
                    std::string edited_bone = display_edit.bone_names[bone_index];
                    const std::string label = "Target Bone " + std::to_string(bone_index + 1U);
                    if (draw_string_combo(label.c_str(), bone_options, &edited_bone)) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index =
                                ensure_transform_constraint_edit_index(state, selected_name)) {
                            project->transform_constraint_edits[*edit_index].bone_names[bone_index] =
                                edited_bone;
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Transform,
                                project->transform_constraint_edits[*edit_index].name,
                                "Transform constraint edit failed",
                                "Updated transform target selection on " + selected_name);
                        }
                    }
                }

                const auto update_mix = [&](const char* label,
                                            double value,
                                            auto setter,
                                            std::string status) {
                    double edited_value = value;
                    if (ImGui::SliderScalar(
                            label,
                            ImGuiDataType_Double,
                            &edited_value,
                            &kZero,
                            &kOne,
                            "%.2f")) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index =
                                ensure_transform_constraint_edit_index(state, selected_name)) {
                            setter(&project->transform_constraint_edits[*edit_index], edited_value);
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Transform,
                                project->transform_constraint_edits[*edit_index].name,
                                "Transform constraint edit failed",
                                std::move(status));
                        }
                    }
                };
                update_mix(
                    "Rotate Mix",
                    display_edit.rotate_mix,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->rotate_mix = value;
                    },
                    "Updated transform rotate mix on " + selected_name);
                update_mix(
                    "Translate Mix",
                    display_edit.translate_mix,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->translate_mix = value;
                    },
                    "Updated transform translate mix on " + selected_name);
                update_mix(
                    "Scale Mix",
                    display_edit.scale_mix,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->scale_mix = value;
                    },
                    "Updated transform scale mix on " + selected_name);
                update_mix(
                    "Shear Mix",
                    display_edit.shear_mix,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->shear_mix = value;
                    },
                    "Updated transform shear mix on " + selected_name);

                const auto update_offset = [&](const char* label,
                                               double value,
                                               auto setter,
                                               std::string status) {
                    double edited_value = value;
                    if (ImGui::DragScalar(
                            label,
                            ImGuiDataType_Double,
                            &edited_value,
                            0.1f,
                            nullptr,
                            nullptr,
                            "%.3f")) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index =
                                ensure_transform_constraint_edit_index(state, selected_name)) {
                            setter(&project->transform_constraint_edits[*edit_index], edited_value);
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Transform,
                                project->transform_constraint_edits[*edit_index].name,
                                "Transform constraint edit failed",
                                std::move(status));
                        }
                    }
                };
                update_offset(
                    "Offset Rotation",
                    display_edit.offsets.rotation,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.rotation = value;
                    },
                    "Updated transform rotation offset on " + selected_name);
                update_offset(
                    "Offset X",
                    display_edit.offsets.x,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.x = value;
                    },
                    "Updated transform X offset on " + selected_name);
                update_offset(
                    "Offset Y",
                    display_edit.offsets.y,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.y = value;
                    },
                    "Updated transform Y offset on " + selected_name);
                update_offset(
                    "Offset Scale X",
                    display_edit.offsets.scale_x,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.scale_x = value;
                    },
                    "Updated transform scaleX offset on " + selected_name);
                update_offset(
                    "Offset Scale Y",
                    display_edit.offsets.scale_y,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.scale_y = value;
                    },
                    "Updated transform scaleY offset on " + selected_name);
                update_offset(
                    "Offset Shear X",
                    display_edit.offsets.shear_x,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.shear_x = value;
                    },
                    "Updated transform shearX offset on " + selected_name);
                update_offset(
                    "Offset Shear Y",
                    display_edit.offsets.shear_y,
                    [](marrow::editor::TransformConstraintEdit* edit, double value) {
                        edit->offsets.shear_y = value;
                    },
                    "Updated transform shearY offset on " + selected_name);
            }

            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Physics")) {
            if (ImGui::Button("Add Physics Constraint")) {
                const marrow::editor::ProjectData previous_project = *project;
                if (const auto new_edit = make_default_physics_constraint_edit(*state)) {
                    project->physics_constraint_edits.push_back(*new_edit);
                    commit_constraint_change(
                        previous_project,
                        ConstraintEditKind::Physics,
                        new_edit->name,
                        "Physics constraint edit failed",
                        "Added physics constraint " + new_edit->name);
                } else {
                    state->status_message = "Could not derive a valid default physics constraint";
                }
            }

            ImGui::BeginChild("physics_constraint_list", ImVec2(0.0f, 120.0f), true);
            for (const auto& constraint : skeleton.physics_constraints()) {
                const bool selected =
                    state->selected_constraint.has_value() &&
                    state->selected_constraint->kind == ConstraintEditKind::Physics &&
                    state->selected_constraint->name == constraint.name;
                const bool has_project_edit =
                    project->find_physics_constraint_edit(constraint.name) != nullptr;
                const std::string label =
                    constraint.name + (has_project_edit ? "  [project]" : "  [runtime]");
                if (ImGui::Selectable(label.c_str(), selected)) {
                    select_constraint(
                        state,
                        ConstraintEditKind::Physics,
                        constraint.name,
                        "Constraints",
                        true);
                }
            }
            ImGui::EndChild();

            const std::string selected_name =
                state->selected_constraint.has_value() &&
                    state->selected_constraint->kind == ConstraintEditKind::Physics &&
                    find_named_constraint(
                        skeleton.physics_constraints(),
                        state->selected_constraint->name) != nullptr
                ? state->selected_constraint->name
                : (!skeleton.physics_constraints().empty()
                       ? skeleton.physics_constraints().front().name
                       : std::string{});
            if (selected_name.empty()) {
                ImGui::TextUnformatted(
                    "No physics constraints are active in the current runtime preview.");
            } else {
                const auto runtime_edit =
                    make_physics_constraint_edit_from_runtime(*state, selected_name);
                const marrow::editor::PhysicsConstraintEdit* project_edit =
                    project->find_physics_constraint_edit(selected_name);
                const marrow::editor::PhysicsConstraintEdit display_edit =
                    project_edit != nullptr ? *project_edit : *runtime_edit;

                ImGui::Separator();
                ImGui::Text("Name: %s", display_edit.name.c_str());
                ImGui::Text(
                    "Source: %s",
                    project_edit != nullptr ? "project constraint edit" : "runtime constraint");

                if (ImGui::Button("Add Chain Bone##physics")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_physics_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->physics_constraint_edits[*edit_index];
                        if (append_direct_child_name(&edit.bone_names)) {
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Physics,
                                edit.name,
                                "Physics constraint edit failed",
                                "Extended the physics chain on " + selected_name);
                        } else {
                            *project = previous_project;
                            state->status_message =
                                "Could not extend the physics chain with a direct child bone";
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove Chain Bone##physics")) {
                    const marrow::editor::ProjectData previous_project = *project;
                    if (const auto edit_index = ensure_physics_constraint_edit_index(state, selected_name)) {
                        auto& edit = project->physics_constraint_edits[*edit_index];
                        if (edit.bone_names.size() > 1U) {
                            edit.bone_names.pop_back();
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Physics,
                                edit.name,
                                "Physics constraint edit failed",
                                "Shortened the physics chain on " + selected_name);
                        }
                    }
                }

                for (std::size_t bone_index = 0; bone_index < display_edit.bone_names.size(); ++bone_index) {
                    std::string edited_bone = display_edit.bone_names[bone_index];
                    const std::string label = "Chain Bone " + std::to_string(bone_index + 1U) +
                        "##physics";
                    if (draw_string_combo(label.c_str(), bone_options, &edited_bone)) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index = ensure_physics_constraint_edit_index(state, selected_name)) {
                            project->physics_constraint_edits[*edit_index].bone_names[bone_index] =
                                edited_bone;
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Physics,
                                project->physics_constraint_edits[*edit_index].name,
                                "Physics constraint edit failed",
                                "Updated physics chain selection on " + selected_name);
                        }
                    }
                }

                auto update_positive_value = [&](const char* label,
                                                 double value,
                                                 auto setter,
                                                 double max_value,
                                                 std::string status) {
                    double edited_value = value;
                    if (ImGui::SliderScalar(
                            label,
                            ImGuiDataType_Double,
                            &edited_value,
                            &kZero,
                            &max_value,
                            "%.2f")) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index =
                                ensure_physics_constraint_edit_index(state, selected_name)) {
                            setter(&project->physics_constraint_edits[*edit_index], edited_value);
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Physics,
                                project->physics_constraint_edits[*edit_index].name,
                                "Physics constraint edit failed",
                                std::move(status));
                        }
                    }
                };
                update_positive_value(
                    "Inertia",
                    display_edit.inertia,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->inertia = value;
                    },
                    kOne,
                    "Updated physics inertia on " + selected_name);
                update_positive_value(
                    "Damping",
                    display_edit.damping,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->damping = value;
                    },
                    kTen,
                    "Updated physics damping on " + selected_name);
                update_positive_value(
                    "Strength",
                    display_edit.strength,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->strength = value;
                    },
                    50.0,
                    "Updated physics strength on " + selected_name);
                update_positive_value(
                    "Mix##physics",
                    display_edit.mix,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->mix = value;
                    },
                    kOne,
                    "Updated physics mix on " + selected_name);

                const auto update_force = [&](const char* label,
                                              double value,
                                              auto setter,
                                              std::string status) {
                    double edited_value = value;
                    if (ImGui::DragScalar(
                            label,
                            ImGuiDataType_Double,
                            &edited_value,
                            0.5f,
                            nullptr,
                            nullptr,
                            "%.2f")) {
                        const marrow::editor::ProjectData previous_project = *project;
                        if (const auto edit_index =
                                ensure_physics_constraint_edit_index(state, selected_name)) {
                            setter(&project->physics_constraint_edits[*edit_index], edited_value);
                            commit_constraint_change(
                                previous_project,
                                ConstraintEditKind::Physics,
                                project->physics_constraint_edits[*edit_index].name,
                                "Physics constraint edit failed",
                                std::move(status));
                        }
                    }
                };
                update_force(
                    "Gravity X",
                    display_edit.gravity.x,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->gravity.x = value;
                    },
                    "Updated physics gravity X on " + selected_name);
                update_force(
                    "Gravity Y",
                    display_edit.gravity.y,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->gravity.y = value;
                    },
                    "Updated physics gravity Y on " + selected_name);
                update_force(
                    "Wind X",
                    display_edit.wind.x,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->wind.x = value;
                    },
                    "Updated physics wind X on " + selected_name);
                update_force(
                    "Wind Y",
                    display_edit.wind.y,
                    [](marrow::editor::PhysicsConstraintEdit* edit, double value) {
                        edit->wind.y = value;
                    },
                    "Updated physics wind Y on " + selected_name);
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void draw_hierarchy_node(
    ShellState* state,
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::vector<std::size_t>>& children,
    std::size_t bone_index) {
    const auto& bone = skeleton.bones()[bone_index];
    const bool selected =
        state->selected_bone_index.has_value() && *state->selected_bone_index == bone_index;
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (selected) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }
    if (children[bone_index].empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    } else {
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    }

    ImGui::PushID(static_cast<int>(bone_index));
    const bool open = ImGui::TreeNodeEx(
        "bone",
        flags,
        "%s%s",
        bone.name.c_str(),
        state->preview_skeleton && !state->preview_skeleton->is_bone_active(bone_index)
            ? " (inactive)"
            : "");
    if (ImGui::IsItemClicked()) {
        select_bone(state, bone_index, "Hierarchy", true);
    }

    if (open && !(flags & ImGuiTreeNodeFlags_NoTreePushOnOpen)) {
        for (const std::size_t child_index : children[bone_index]) {
            draw_hierarchy_node(state, skeleton, children, child_index);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}

void draw_hierarchy_window(ShellState* state) {
    ImGui::Begin("Hierarchy");

    if (!state->load_result) {
        ImGui::TextUnformatted("Load a valid project to inspect skeleton bones.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    const auto children = build_bone_children(skeleton);
    ImGui::Text("Bones: %zu", skeleton.bones().size());
    ImGui::Separator();

    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        if (!skeleton.bones()[bone_index].parent_index.has_value()) {
            draw_hierarchy_node(state, skeleton, children, bone_index);
        }
    }

    ImGui::End();
}

void draw_timeline_lane(
    ShellState* state,
    const TimelineTrackRow& track,
    double duration_seconds) {
    ImGui::PushID(track.id.c_str());

    const bool selected = timeline_track_matches_selection(*state, track);
    const float lane_height = 24.0f;
    const float lane_width = std::max(96.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("timeline_lane", ImVec2(lane_width, lane_height));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    const float rect_width = std::max(1.0f, rect_max.x - rect_min.x);
    draw_list->AddRectFilled(
        rect_min,
        rect_max,
        selected ? IM_COL32(78, 52, 25, 230) : IM_COL32(28, 31, 36, 235),
        4.0f);
    draw_list->AddRect(
        rect_min,
        rect_max,
        selected ? IM_COL32(230, 180, 97, 255) : IM_COL32(74, 80, 88, 255),
        4.0f);

    for (int tick_index = 1; tick_index < 4; ++tick_index) {
        const float normalized = static_cast<float>(tick_index) / 4.0f;
        const float tick_x = rect_min.x + (rect_width * normalized);
        draw_list->AddLine(
            ImVec2(tick_x, rect_min.y + 2.0f),
            ImVec2(tick_x, rect_max.y - 2.0f),
            IM_COL32(56, 61, 69, 180));
    }

    if (duration_seconds > 0.0) {
        const float playhead_x = rect_min.x +
            static_cast<float>(state->timeline_time_seconds / duration_seconds) * rect_width;
        draw_list->AddLine(
            ImVec2(playhead_x, rect_min.y),
            ImVec2(playhead_x, rect_max.y),
            IM_COL32(247, 204, 114, 255),
            2.0f);
    }

    const float marker_half = 4.0f;
    for (const double key_time : track.key_times) {
        const float normalized = duration_seconds > 0.0
            ? static_cast<float>(std::clamp(key_time / duration_seconds, 0.0, 1.0))
            : 0.0f;
        const float marker_x = rect_min.x + (rect_width * normalized);
        const bool near_playhead =
            std::abs(key_time - state->timeline_time_seconds) <= 1e-6;
        const ImU32 fill_color = near_playhead
            ? IM_COL32(250, 233, 188, 255)
            : IM_COL32(221, 164, 72, 255);
        const ImU32 outline_color = IM_COL32(37, 40, 46, 255);
        draw_list->AddQuadFilled(
            ImVec2(marker_x, rect_min.y + 4.0f),
            ImVec2(marker_x + marker_half, rect_min.y + 4.0f + marker_half),
            ImVec2(marker_x, rect_min.y + 4.0f + (marker_half * 2.0f)),
            ImVec2(marker_x - marker_half, rect_min.y + 4.0f + marker_half),
            fill_color);
        draw_list->AddQuad(
            ImVec2(marker_x, rect_min.y + 4.0f),
            ImVec2(marker_x + marker_half, rect_min.y + 4.0f + marker_half),
            ImVec2(marker_x, rect_min.y + 4.0f + (marker_half * 2.0f)),
            ImVec2(marker_x - marker_half, rect_min.y + 4.0f + marker_half),
            outline_color,
            1.0f);
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float local_x = std::clamp(ImGui::GetIO().MousePos.x - rect_min.x, 0.0f, rect_width);
        const double clicked_time = duration_seconds > 0.0
            ? (static_cast<double>(local_x) / static_cast<double>(rect_width)) * duration_seconds
            : 0.0;
        const double threshold_time =
            duration_seconds > 0.0
                ? (12.0 / static_cast<double>(rect_width)) * duration_seconds
                : 0.0;
        const std::optional<double> snapped_time =
            nearest_key_time(track, clicked_time, threshold_time);
        state->timeline_playing = false;
        focus_timeline_track(
            state,
            track,
            snapped_time.value_or(clicked_time),
            "Timeline",
            true);
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

    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message) {
        if (!rebuild_project_runtime(state)) {
            const std::string rebuild_error = state->error_message;
            *state->load_result.project = previous_project;
            rebuild_project_runtime(state);
            state->error_message = rebuild_error;
            state->status_message = "Draw-order edit failed";
            return false;
        }

        state->project_dirty = true;
        state->selected_timeline_track_id = track.id;
        state->status_message = std::move(status_message);
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
                    "Added a draw-order key on " + track.animation_name);
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
            double edited_time = display_key.time;
            if (ImGui::DragScalar(
                    "Time",
                    ImGuiDataType_Double,
                    &edited_time,
                    0.01f,
                    nullptr,
                    nullptr,
                    "%.3f s")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_draw_order_timeline_edit_index(state, track)) {
                    auto& editable_track =
                        state->load_result.project->draw_order_timeline_edits[*edit_index];
                    editable_track.keyframes[key_index].time = clamp_existing_key_time(
                        editable_track.keyframes,
                        key_index,
                        edited_time,
                        duration_seconds);
                    commit_project_change(
                        previous_project,
                        "Updated draw-order key timing on " + track.animation_name);
                }
            }

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

    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message) {
        if (!rebuild_project_runtime(state)) {
            const std::string rebuild_error = state->error_message;
            *state->load_result.project = previous_project;
            rebuild_project_runtime(state);
            state->error_message = rebuild_error;
            state->status_message = "Event edit failed";
            return false;
        }

        state->project_dirty = true;
        state->selected_timeline_track_id = track.id;
        state->status_message = std::move(status_message);
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
                "Added an event key on " + track.animation_name);
        }
    }

    ImGui::BeginChild("event_key_editor", ImVec2(0.0f, 340.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto& display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time) + " / " + display_key.event_name;
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            double edited_time = display_key.time;
            if (ImGui::DragScalar(
                    "Time",
                    ImGuiDataType_Double,
                    &edited_time,
                    0.01f,
                    nullptr,
                    nullptr,
                    "%.3f s")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                    auto& editable_track =
                        state->load_result.project->event_timeline_edits[*edit_index];
                    editable_track.keyframes[key_index].time =
                        clamp_existing_non_decreasing_key_time(
                            editable_track.keyframes,
                            key_index,
                            edited_time);
                    commit_project_change(
                        previous_project,
                        "Updated event key timing on " + track.animation_name);
                }
            }

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
                                              std::string status) {
                bool enabled = value.has_value();
                if (ImGui::Checkbox(toggle_label, &enabled)) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        setter(
                            &state->load_result.project->event_timeline_edits[*edit_index]
                                 .keyframes[key_index],
                            enabled ? std::optional<double>(default_value) : std::nullopt);
                        commit_project_change(previous_project, std::move(status));
                    }
                }
                if (!enabled) {
                    return;
                }

                double edited_value = value.value_or(default_value);
                if (ImGui::DragScalar(
                        value_label,
                        ImGuiDataType_Double,
                        &edited_value,
                        0.05f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        setter(
                            &state->load_result.project->event_timeline_edits[*edit_index]
                                 .keyframes[key_index],
                            std::optional<double>(edited_value));
                        commit_project_change(previous_project, std::move(status));
                    }
                }
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
                "Updated event float override");
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
                "Updated event volume override");
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
                "Updated event balance override");

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

    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message) {
        if (!rebuild_project_runtime(state)) {
            const std::string rebuild_error = state->error_message;
            *state->load_result.project = previous_project;
            rebuild_project_runtime(state);
            state->error_message = rebuild_error;
            state->status_message = "Timeline edit failed";
            return false;
        }

        state->project_dirty = true;
        state->selected_timeline_track_id = track->id;
        state->status_message = std::move(status_message);
        return true;
    };

    if (ImGui::Button("Add Key At Playhead")) {
        const marrow::editor::ProjectData previous_project = *state->load_result.project;
        const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
        if (edit_index.has_value()) {
            auto& editable_track =
                state->load_result.project->transform_timeline_edits[*edit_index];
            if (const auto insert_time = insertable_key_time(
                    editable_track.keyframes,
                    state->timeline_time_seconds,
                    duration_seconds)) {
                marrow::editor::TransformKeyframeEdit new_key =
                    sample_transform_keyframe(*state, *track);
                new_key.time = *insert_time;
                const auto iterator = std::upper_bound(
                    editable_track.keyframes.begin(),
                    editable_track.keyframes.end(),
                    new_key.time,
                    [](double time, const marrow::editor::TransformKeyframeEdit& keyframe) {
                        return time < keyframe.time;
                    });
                editable_track.keyframes.insert(iterator, std::move(new_key));
                commit_project_change(
                    previous_project,
                    "Added a " + std::string(transform_channel_label(*track->transform_channel)) +
                        " key on " + bone_name);
            } else {
                *state->load_result.project = previous_project;
                state->status_message = "Could not place a new key between existing keyframes";
            }
        }
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
            double edited_time = display_key.time;
            if (ImGui::DragScalar(
                    "Time",
                    ImGuiDataType_Double,
                    &edited_time,
                    0.01f,
                    nullptr,
                    nullptr,
                    "%.3f s")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                if (edit_index.has_value()) {
                    auto& editable_track =
                        state->load_result.project->transform_timeline_edits[*edit_index];
                    editable_track.keyframes[key_index].time = clamp_existing_key_time(
                        editable_track.keyframes,
                        key_index,
                        edited_time,
                        duration_seconds);
                    commit_project_change(
                        previous_project,
                        "Updated key timing on " + bone_name + " " +
                            std::string(transform_channel_label(*track->transform_channel)));
                }
            }

            if (*track->transform_channel == marrow::editor::TransformTimelineChannel::Rotate) {
                double edited_angle = display_key.angle;
                if (ImGui::DragScalar(
                        "Angle",
                        ImGuiDataType_Double,
                        &edited_angle,
                        0.1f,
                        nullptr,
                        nullptr,
                        "%.3f deg")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                    if (edit_index.has_value()) {
                        state->load_result.project->transform_timeline_edits[*edit_index]
                            .keyframes[key_index]
                            .angle = edited_angle;
                        commit_project_change(
                            previous_project,
                            "Updated key angle on " + bone_name);
                    }
                }
            } else {
                double edited_x = display_key.x;
                if (ImGui::DragScalar(
                        "X",
                        ImGuiDataType_Double,
                        &edited_x,
                        0.1f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                    if (edit_index.has_value()) {
                        state->load_result.project->transform_timeline_edits[*edit_index]
                            .keyframes[key_index]
                            .x = edited_x;
                        commit_project_change(
                            previous_project,
                            "Updated key X on " + bone_name);
                    }
                }

                double edited_y = display_key.y;
                if (ImGui::DragScalar(
                        "Y",
                        ImGuiDataType_Double,
                        &edited_y,
                        0.1f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                    if (edit_index.has_value()) {
                        state->load_result.project->transform_timeline_edits[*edit_index]
                            .keyframes[key_index]
                            .y = edited_y;
                        commit_project_change(
                            previous_project,
                            "Updated key Y on " + bone_name);
                    }
                }
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
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
                        break;
                    }
                    }
                    commit_project_change(
                        previous_project,
                        "Updated interpolation on " + bone_name + " " +
                            std::string(transform_channel_label(*track->transform_channel)));
                }
            }

            if (display_key.interpolation.kind() ==
                marrow::runtime::InterpolationKind::CubicBezier) {
                marrow::runtime::CubicBezierControlPoints bezier =
                    display_key.interpolation.cubic_bezier();

                if (ImGui::DragScalar(
                        "Bezier X1",
                        ImGuiDataType_Double,
                        &bezier.cx1,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    bezier.cx1 = std::clamp(bezier.cx1, 0.0, 1.0);
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                    if (edit_index.has_value()) {
                        auto& editable_key =
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index];
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
                        commit_project_change(previous_project, "Updated bezier control point X1");
                    }
                }
                if (ImGui::DragScalar(
                        "Bezier Y1",
                        ImGuiDataType_Double,
                        &bezier.cy1,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                    if (edit_index.has_value()) {
                        auto& editable_key =
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index];
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
                        commit_project_change(previous_project, "Updated bezier control point Y1");
                    }
                }
                if (ImGui::DragScalar(
                        "Bezier X2",
                        ImGuiDataType_Double,
                        &bezier.cx2,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    bezier.cx2 = std::clamp(bezier.cx2, 0.0, 1.0);
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                    if (edit_index.has_value()) {
                        auto& editable_key =
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index];
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
                        commit_project_change(previous_project, "Updated bezier control point X2");
                    }
                }
                if (ImGui::DragScalar(
                        "Bezier Y2",
                        ImGuiDataType_Double,
                        &bezier.cy2,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                    if (edit_index.has_value()) {
                        auto& editable_key =
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index];
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
                        commit_project_change(previous_project, "Updated bezier control point Y2");
                    }
                }
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

    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message) {
        if (!rebuild_project_runtime(state)) {
            const std::string rebuild_error = state->error_message;
            *state->load_result.project = previous_project;
            rebuild_project_runtime(state);
            state->error_message = rebuild_error;
            state->status_message = "Mesh deform edit failed";
            return false;
        }

        state->project_dirty = true;
        state->selected_timeline_track_id = track.id;
        state->status_message = std::move(status_message);
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
                        *track.deform_attachment_name);
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
            double edited_time = display_key.time;
            if (ImGui::DragScalar(
                    "Time",
                    ImGuiDataType_Double,
                    &edited_time,
                    0.01f,
                    nullptr,
                    nullptr,
                    "%.3f s")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                if (edit_index.has_value()) {
                    auto& editable_track =
                        state->load_result.project->mesh_deform_timeline_edits[*edit_index];
                    editable_track.keyframes[key_index].time = clamp_existing_key_time(
                        editable_track.keyframes,
                        key_index,
                        edited_time,
                        duration_seconds);
                    commit_project_change(
                        previous_project,
                        "Updated deform key timing on " + slot_name);
                }
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
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
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

                if (ImGui::DragScalar(
                        "Bezier X1",
                        ImGuiDataType_Double,
                        &bezier.cx1,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    bezier.cx1 = std::clamp(bezier.cx1, 0.0, 1.0);
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                    if (edit_index.has_value()) {
                        auto& editable_key =
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                                .keyframes[key_index];
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
                        commit_project_change(previous_project, "Updated deform bezier control point X1");
                    }
                }
                if (ImGui::DragScalar(
                        "Bezier Y1",
                        ImGuiDataType_Double,
                        &bezier.cy1,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                    if (edit_index.has_value()) {
                        auto& editable_key =
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                                .keyframes[key_index];
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
                        commit_project_change(previous_project, "Updated deform bezier control point Y1");
                    }
                }
                if (ImGui::DragScalar(
                        "Bezier X2",
                        ImGuiDataType_Double,
                        &bezier.cx2,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    bezier.cx2 = std::clamp(bezier.cx2, 0.0, 1.0);
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                    if (edit_index.has_value()) {
                        auto& editable_key =
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                                .keyframes[key_index];
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
                        commit_project_change(previous_project, "Updated deform bezier control point X2");
                    }
                }
                if (ImGui::DragScalar(
                        "Bezier Y2",
                        ImGuiDataType_Double,
                        &bezier.cy2,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                    if (edit_index.has_value()) {
                        auto& editable_key =
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                                .keyframes[key_index];
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                bezier.cx1,
                                bezier.cy1,
                                bezier.cx2,
                                bezier.cy2);
                        commit_project_change(previous_project, "Updated deform bezier control point Y2");
                    }
                }
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Vertex Offsets");
            for (std::size_t vertex_index = 0; vertex_index < display_key.vertex_offsets.size() / 2U;
                 ++vertex_index) {
                ImGui::PushID(static_cast<int>(vertex_index));
                const std::size_t x_index = vertex_index * 2U;
                const std::size_t y_index = x_index + 1U;

                double edited_x = display_key.vertex_offsets[x_index];
                if (ImGui::DragScalar(
                        "X",
                        ImGuiDataType_Double,
                        &edited_x,
                        0.25f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                    if (edit_index.has_value()) {
                        state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                            .keyframes[key_index]
                            .vertex_offsets[x_index] = edited_x;
                        commit_project_change(
                            previous_project,
                            "Updated deform vertex X on " + slot_name + " / " +
                                *track.deform_attachment_name);
                    }
                }

                ImGui::SameLine();
                double edited_y = display_key.vertex_offsets[y_index];
                if (ImGui::DragScalar(
                        "Y",
                        ImGuiDataType_Double,
                        &edited_y,
                        0.25f,
                        nullptr,
                        nullptr,
                        "%.3f")) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                    if (edit_index.has_value()) {
                        state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                            .keyframes[key_index]
                            .vertex_offsets[y_index] = edited_y;
                        commit_project_change(
                            previous_project,
                            "Updated deform vertex Y on " + slot_name + " / " +
                                *track.deform_attachment_name);
                    }
                }

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

void draw_timeline_window(ShellState* state) {
    ImGui::Begin("Timeline");

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
            if (ImGui::Selectable(animation.name.c_str(), selected)) {
                state->timeline_playing = false;
                set_selected_animation(state, animation.name, "Timeline", true, true);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    normalize_state_preview_settings(state);
    const auto apply_state_preview_change = [&](std::string status_message) {
        state->timeline_playing = false;
        refresh_preview_pose(state);
        state->status_message = std::move(status_message);
    };

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
                if (ImGui::Selectable(preview_animation.name.c_str(), selected)) {
                    state->preview_queued_animation_name = preview_animation.name;
                    apply_state_preview_change(
                        "Queued " + preview_animation.name + " after " +
                        state->selected_animation_name);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
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

    const marrow::runtime::AnimationData* animation = selected_animation(*state);
    const double duration_seconds = timeline_preview_duration(*state);
    const std::vector<TimelineTrackRow> tracks =
        animation != nullptr ? build_timeline_tracks(skeleton, *animation)
                             : std::vector<TimelineTrackRow>{};

    if (ImGui::Button(state->timeline_playing ? "Pause" : "Play")) {
        if (animation != nullptr) {
            state->timeline_playing = !state->timeline_playing;
            state->status_message =
                std::string(state->timeline_playing ? "Playing " : "Paused ") +
                state->selected_animation_name;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        state->timeline_playing = false;
        scrub_timeline_time(state, 0.0, "Timeline", true);
    }
    ImGui::SameLine();
    bool loop_enabled = state->timeline_loop;
    if (ImGui::Checkbox("Loop", &loop_enabled)) {
        state->timeline_loop = loop_enabled;
        refresh_preview_pose(state);
        state->status_message =
            std::string(loop_enabled ? "Enabled" : "Disabled") + " timeline looping";
    }
    ImGui::SameLine();
    if (ImGui::Button("Prev Key")) {
        if (const auto previous_key = adjacent_key_time(
                tracks,
                state->timeline_time_seconds,
                false)) {
            state->timeline_playing = false;
            scrub_timeline_time(state, *previous_key, "Timeline", true);
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Next Key")) {
        if (const auto next_key = adjacent_key_time(
                tracks,
                state->timeline_time_seconds,
                true)) {
            state->timeline_playing = false;
            scrub_timeline_time(state, *next_key, "Timeline", true);
        }
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

    if (tracks.empty()) {
        ImGui::TextUnformatted("The selected animation does not contain keyed tracks.");
        ImGui::End();
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
            const std::string label =
                track.label + " (" + std::to_string(track.key_times.size()) + ")";
            if (ImGui::Selectable(label.c_str(), selected)) {
                state->timeline_playing = false;
                focus_timeline_track(
                    state,
                    track,
                    state->timeline_time_seconds,
                    "Timeline",
                    true);
            }

            ImGui::TableSetColumnIndex(1);
            draw_timeline_lane(state, track, duration_seconds);
        }

        ImGui::EndTable();
    }

    draw_transform_timeline_editor(state, tracks);

    ImGui::End();
}

void draw_viewport_window(ShellState* state) {
    ImGui::Begin("Viewport");
    const std::string preview_label =
        state->selected_animation_name.empty() ? std::string("Setup pose preview")
                                               : "Animation preview / " + state->selected_animation_name;
    ImGui::TextUnformatted(preview_label.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("LMB select  RMB drag pan  Wheel zoom");
    if (!state->selected_animation_name.empty()) {
        ImGui::TextDisabled(
            "%s / %s",
            format_time_seconds(state->timeline_time_seconds).c_str(),
            format_time_seconds(timeline_preview_duration(*state)).c_str());
    }
    ImGui::Separator();

    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 32.0f || canvas_size.y < 32.0f) {
        ImGui::End();
        return;
    }

    const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(
        "viewport_canvas",
        canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
        const ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
        state->viewport.pan_x += mouse_delta.x;
        state->viewport.pan_y += mouse_delta.y;
    }

    if (hovered && std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
        const float zoom_factor = ImGui::GetIO().MouseWheel > 0.0f ? 1.1f : 0.9f;
        state->viewport.zoom = clamp_zoom(static_cast<float>(state->viewport.zoom) * zoom_factor);
    }

    const auto layout = build_viewport_layout(*state, canvas_origin, canvas_size);
    const ImVec2 canvas_end(
        canvas_origin.x + canvas_size.x,
        canvas_origin.y + canvas_size.y);
    draw_list->AddRectFilled(canvas_origin, canvas_end, IM_COL32(18, 21, 25, 255), 6.0f);
    draw_list->AddRect(canvas_origin, canvas_end, IM_COL32(56, 61, 69, 255), 6.0f);

    if (!layout.has_value()) {
        draw_list->AddText(
            ImVec2(canvas_origin.x + 16.0f, canvas_origin.y + 16.0f),
            IM_COL32(240, 232, 213, 255),
            "Project load failed. Reload a valid .marrow file.");
        ImGui::End();
        return;
    }

    const float grid_spacing = std::max(18.0f, 40.0f * static_cast<float>(state->viewport.zoom));
    for (float x = first_grid_line(layout->world_origin_screen.x, canvas_origin.x, grid_spacing);
         x < layout->canvas_end.x;
         x += grid_spacing) {
        draw_list->AddLine(
            ImVec2(x, canvas_origin.y),
            ImVec2(x, layout->canvas_end.y),
            IM_COL32(31, 35, 41, 255));
    }
    for (float y = first_grid_line(layout->world_origin_screen.y, canvas_origin.y, grid_spacing);
         y < layout->canvas_end.y;
         y += grid_spacing) {
        draw_list->AddLine(
            ImVec2(canvas_origin.x, y),
            ImVec2(layout->canvas_end.x, y),
            IM_COL32(31, 35, 41, 255));
    }

    draw_list->AddLine(
        ImVec2(canvas_origin.x, layout->world_origin_screen.y),
        ImVec2(layout->canvas_end.x, layout->world_origin_screen.y),
        IM_COL32(189, 86, 37, 255),
        1.5f);
    draw_list->AddLine(
        ImVec2(layout->world_origin_screen.x, canvas_origin.y),
        ImVec2(layout->world_origin_screen.x, layout->canvas_end.y),
        IM_COL32(204, 177, 110, 255),
        1.5f);

    std::optional<std::size_t> hovered_bone;
    if (hovered) {
        hovered_bone = pick_bone_at_position(*layout, ImGui::GetIO().MousePos);
    }
    if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hovered_bone.has_value()) {
        select_bone(state, *hovered_bone, "Viewport", true);
    }

    for (const BoneCanvasNode& node : layout->bones) {
        if (!node.parent_index.has_value() || *node.parent_index >= layout->bones.size()) {
            continue;
        }

        const BoneCanvasNode& parent = layout->bones[*node.parent_index];
        const bool selected =
            state->selected_bone_index.has_value() && *state->selected_bone_index == node.bone_index;
        const ImU32 line_color = selected
            ? IM_COL32(247, 204, 114, 255)
            : node.active ? IM_COL32(214, 163, 76, 220) : IM_COL32(111, 117, 125, 180);
        draw_list->AddLine(
            parent.screen_position,
            node.screen_position,
            line_color,
            selected ? 3.0f : 2.0f);
    }

    const auto& bones = state->load_result.skeleton_data->bones();
    for (const BoneCanvasNode& node : layout->bones) {
        const bool selected =
            state->selected_bone_index.has_value() && *state->selected_bone_index == node.bone_index;
        const bool hovered_selection =
            hovered_bone.has_value() && *hovered_bone == node.bone_index;
        const float radius = layout->joint_radius + (selected ? 2.0f : 0.0f);
        const ImU32 fill_color = selected
            ? IM_COL32(247, 204, 114, 255)
            : hovered_selection ? IM_COL32(226, 186, 97, 240)
                                : node.active ? IM_COL32(208, 134, 57, 230)
                                              : IM_COL32(98, 103, 110, 200);
        const ImU32 outline_color =
            node.active ? IM_COL32(33, 37, 41, 255) : IM_COL32(48, 50, 54, 255);
        draw_list->AddCircleFilled(node.screen_position, radius, fill_color, 18);
        draw_list->AddCircle(node.screen_position, radius, outline_color, 18, 1.5f);

        if (selected || hovered_selection || layout->bones.size() <= 12) {
            draw_list->AddText(
                ImVec2(node.screen_position.x + radius + 6.0f, node.screen_position.y - 6.0f),
                selected ? IM_COL32(247, 232, 191, 255) : IM_COL32(225, 212, 180, 220),
                bones[node.bone_index].name.c_str());
        }
    }

    if (hovered_bone.has_value()) {
        ImGui::SetTooltip("%s", bones[*hovered_bone].name.c_str());
    }

    if (state->selected_bone_index.has_value() &&
        *state->selected_bone_index < bones.size()) {
        std::ostringstream selection_stream;
        selection_stream << bones[*state->selected_bone_index].name
                         << "  pan(" << static_cast<int>(state->viewport.pan_x) << ", "
                         << static_cast<int>(state->viewport.pan_y) << ")"
                         << "  zoom " << state->viewport.zoom;
        draw_list->AddText(
            ImVec2(canvas_origin.x + 14.0f, canvas_origin.y + 12.0f),
            IM_COL32(244, 230, 197, 255),
            selection_stream.str().c_str());
    }

    ImGui::End();
}

void draw_inspector_window(ShellState* state) {
    ImGui::Begin("Inspector");

    if (!state->load_result || !state->preview_skeleton) {
        ImGui::TextUnformatted("Load a valid project to inspect setup-pose data.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    const auto children = build_bone_children(skeleton);
    const auto& world_transforms = state->preview_skeleton->bone_world_transforms();

    ImGui::Text("Viewport pan: %.1f, %.1f", state->viewport.pan_x, state->viewport.pan_y);
    ImGui::Text("Viewport zoom: %.2f", state->viewport.zoom);
    ImGui::Text(
        "Timeline: %s @ %s / %s",
        state->selected_animation_name.empty() ? "<setup pose>" : state->selected_animation_name.c_str(),
        format_time_seconds(state->timeline_time_seconds).c_str(),
        format_time_seconds(timeline_preview_duration(*state)).c_str());
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Bones", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("inspector_bones", ImVec2(0.0f, 140.0f), true);
        for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
            const auto& bone = skeleton.bones()[bone_index];
            const bool selected =
                state->selected_bone_index.has_value() && *state->selected_bone_index == bone_index;
            std::string label = bone.name;
            if (bone.parent_index.has_value() && *bone.parent_index < skeleton.bones().size()) {
                label += " <- " + skeleton.bones()[*bone.parent_index].name;
            }
            if (ImGui::Selectable(
                    (label + "##inspector_bone_" + std::to_string(bone_index)).c_str(),
                    selected)) {
                select_bone(state, bone_index, "Inspector", true);
            }
        }
        ImGui::EndChild();

        if (state->selected_bone_index.has_value() &&
            *state->selected_bone_index < skeleton.bones().size() &&
            *state->selected_bone_index < world_transforms.size()) {
            const std::size_t bone_index = *state->selected_bone_index;
            const auto& bone = skeleton.bones()[bone_index];
            const auto& world = world_transforms[bone_index];
            const auto& setup_pose = bone.setup_pose;

            ImGui::Spacing();
            ImGui::Text("Selected: %s", bone.name.c_str());
            ImGui::Text("Parent: %s", parent_bone_name(skeleton, bone).c_str());
            ImGui::Text("Children: %zu", children[bone_index].size());
            ImGui::Text("Slots: %s", join_slots_for_bone(skeleton, bone_index).c_str());
            ImGui::Text(
                "Active in preview: %s",
                yes_no(state->preview_skeleton->is_bone_active(bone_index)));
            ImGui::Separator();
            ImGui::TextUnformatted("Setup Pose");
            ImGui::Text("Translate: (%.1f, %.1f)", setup_pose.x, setup_pose.y);
            ImGui::Text("Rotation: %.1f deg", setup_pose.rotation);
            ImGui::Text("Scale: (%.2f, %.2f)", setup_pose.scale_x, setup_pose.scale_y);
            ImGui::Text("Shear: (%.1f, %.1f)", setup_pose.shear_x, setup_pose.shear_y);
            ImGui::Text(
                "Inherit: rotation %s, scale %s, reflection %s",
                yes_no(bone.setup_inherit.inherit_rotation),
                yes_no(bone.setup_inherit.inherit_scale),
                yes_no(bone.setup_inherit.inherit_reflection));
            ImGui::Separator();
            ImGui::TextUnformatted("World Pose");
            ImGui::Text("World position: (%.1f, %.1f)", world.world_x, world.world_y);
            ImGui::Text(
                "Basis X: (%.2f, %.2f)  Basis Y: (%.2f, %.2f)",
                world.a,
                world.c,
                world.b,
                world.d);
        } else {
            ImGui::Spacing();
            ImGui::TextUnformatted("Select a bone from the hierarchy or viewport.");
        }
    }

    if (ImGui::CollapsingHeader("Slots", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginChild("inspector_slots", ImVec2(0.0f, 130.0f), true);
        for (std::size_t slot_index = 0; slot_index < skeleton.slots().size(); ++slot_index) {
            const auto& slot = skeleton.slots()[slot_index];
            const auto* current_attachment = state->preview_skeleton->current_attachment(slot_index);
            const bool selected =
                state->selected_slot_index.has_value() && *state->selected_slot_index == slot_index;
            std::string label = slot.name + " -> " +
                (current_attachment != nullptr ? current_attachment->name : std::string("<none>"));
            if (ImGui::Selectable(
                    (label + "##inspector_slot_" + std::to_string(slot_index)).c_str(),
                    selected)) {
                select_slot(state, slot_index, "Inspector", true);
            }
        }
        ImGui::EndChild();

        if (state->selected_slot_index.has_value() &&
            *state->selected_slot_index < skeleton.slots().size() &&
            *state->selected_slot_index < state->preview_skeleton->slot_states().size()) {
            const std::size_t slot_index = *state->selected_slot_index;
            const auto& slot = skeleton.slots()[slot_index];
            const auto& slot_state = state->preview_skeleton->slot_states()[slot_index];
            const auto* current_attachment = state->preview_skeleton->current_attachment(slot_index);
            const auto current_selection = current_attachment_selection(*state, slot_index);
            const auto skin_preview_attachment = resolve_skin_preview_attachment(
                skeleton,
                state->preview_skin_names,
                slot_index);
            bool has_preview_override =
                current_selection.has_value() != skin_preview_attachment.has_value();
            if (!has_preview_override && current_selection.has_value()) {
                has_preview_override =
                    current_selection->attachment_name != skin_preview_attachment->attachment_name ||
                    current_selection->skin_index != skin_preview_attachment->skin_index;
            }

            const std::string dark_color = slot_state.dark_color.has_value()
                ? format_slot_color(*slot_state.dark_color)
                : std::string("<none>");
            const std::string source_skin = current_selection.has_value()
                ? source_skin_name(skeleton, current_selection->skin_index)
                : std::string("<none>");

            ImGui::Spacing();
            ImGui::Text("Selected slot: %s", slot.name.c_str());
            ImGui::Text("Bone: %s", skeleton.bones()[slot.bone_index].name.c_str());
            if (const auto order = draw_order_position(*state->preview_skeleton, slot_index)) {
                ImGui::Text(
                    "Draw order: %zu / %zu",
                    *order + 1U,
                    state->preview_skeleton->draw_order().size());
            }
            ImGui::Text("Blend mode: %s", blend_mode_name(slot.blend_mode));
            ImGui::Text(
                "Setup attachment: %s",
                slot.setup_attachment.empty() ? "<none>" : slot.setup_attachment.c_str());
            ImGui::Text(
                "Preview attachment: %s",
                current_attachment != nullptr ? current_attachment->name.c_str() : "<none>");
            ImGui::Text("Attachment source skin: %s", source_skin.c_str());
            ImGui::Text("Preview override: %s", yes_no(has_preview_override));
            ImGui::Text("Light color: %s", format_slot_color(slot_state.color).c_str());
            ImGui::Text("Dark tint: %s", dark_color.c_str());
        } else {
            ImGui::Spacing();
            ImGui::TextUnformatted("Select a slot to inspect presentation state.");
        }
    }

    if (ImGui::CollapsingHeader("Attachments", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (state->selected_slot_index.has_value() &&
            *state->selected_slot_index < skeleton.slots().size()) {
            const std::size_t slot_index = *state->selected_slot_index;
            const auto attachments = collect_slot_attachments(skeleton, slot_index);

            ImGui::BeginChild("inspector_attachments", ImVec2(0.0f, 130.0f), true);
            if (attachments.empty()) {
                ImGui::TextUnformatted("No attachments are available for this slot.");
            } else {
                for (const auto& attachment_reference : attachments) {
                    bool preview_active = false;
                    if (const auto preview_selection =
                            current_attachment_selection(*state, slot_index)) {
                        preview_active =
                            attachment_matches_selection(*preview_selection, attachment_reference);
                    }

                    const bool selected =
                        state->selected_attachment.has_value() &&
                        attachment_matches_selection(
                            *state->selected_attachment,
                            attachment_reference);
                    std::string label = attachment_reference.attachment->name +
                        " [" + source_skin_name(skeleton, attachment_reference.skin_index) +
                        "] (" + attachment_kind_name(attachment_reference.attachment->kind) + ")";
                    if (preview_active) {
                        label += " [preview]";
                    }
                    if (ImGui::Selectable(
                            (label + "##inspector_attachment_" +
                             std::to_string(slot_index) + "_" +
                             std::to_string(attachment_reference.skin_index.value_or(0)) + "_" +
                             attachment_reference.attachment->name)
                                .c_str(),
                            selected)) {
                        select_attachment(
                            state,
                            AttachmentSelection{
                                slot_index,
                                attachment_reference.skin_index,
                                attachment_reference.attachment->name},
                            "Inspector",
                            true);
                    }
                }
            }
            ImGui::EndChild();

            const auto attachment_reference =
                state->selected_attachment.has_value() &&
                    state->selected_attachment->slot_index == slot_index
                ? resolve_attachment_reference(skeleton, *state->selected_attachment)
                : std::nullopt;

            if (attachment_reference.has_value()) {
                if (ImGui::Button("Apply To Preview Slot")) {
                    apply_attachment_selection_to_preview_slot(
                        state,
                        *state->selected_attachment,
                        "Inspector",
                        true);
                }
            }
            if (state->selected_slot_index.has_value()) {
                if (attachment_reference.has_value()) {
                    ImGui::SameLine();
                }
                if (ImGui::Button("Reset Slot To Skin Preview")) {
                    reset_preview_slot_to_skin_selection(
                        state,
                        slot_index,
                        "Inspector",
                        true);
                }
            }

            if (attachment_reference.has_value()) {
                ImGui::Separator();
                draw_attachment_details(*state, *attachment_reference);
            } else if (!attachments.empty()) {
                ImGui::Spacing();
                ImGui::TextUnformatted("Select an attachment to inspect its data.");
            }
        } else {
            ImGui::TextUnformatted("Select a slot to inspect its attachments.");
        }
    }

    if (ImGui::CollapsingHeader("Skin Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text(
            "Active composition: %s",
            preview_skin_summary(skeleton, state->preview_skin_names).c_str());
        if (const auto default_name = default_skin_name(skeleton)) {
            ImGui::BulletText("%s (base skin)", std::string(*default_name).c_str());
        }

        bool has_toggleable_skin = false;
        for (std::size_t skin_index = 0; skin_index < skeleton.skins().size(); ++skin_index) {
            const auto& skin = skeleton.skins()[skin_index];
            if (is_default_skin_index(skeleton, skin_index)) {
                continue;
            }

            has_toggleable_skin = true;
            bool enabled =
                std::find(
                    state->preview_skin_names.begin(),
                    state->preview_skin_names.end(),
                    skin.name) != state->preview_skin_names.end();
            if (ImGui::Checkbox((skin.name + "##preview_skin").c_str(), &enabled)) {
                set_preview_skin_enabled(state, skin_index, enabled, true);
            }

            std::size_t linked_mesh_count = 0;
            for (const auto& slot_attachment : skin.slot_attachments) {
                if (slot_attachment.attachment.kind == marrow::runtime::AttachmentKind::LinkedMesh) {
                    ++linked_mesh_count;
                }
            }

            ImGui::SameLine();
            ImGui::TextDisabled(
                "%zu slot attachments, %zu linked meshes",
                skin.slot_attachments.size(),
                linked_mesh_count);
        }

        if (!has_toggleable_skin) {
            ImGui::TextUnformatted("No additional skins are available for preview.");
        }
    }

    ImGui::End();
}

void render_shell_frame(GLFWwindow* window, ShellState* shell_state) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    advance_timeline_playback(shell_state, ImGui::GetIO().DeltaTime);

    bool reload_requested = false;
    draw_menu_bar(window, &reload_requested, shell_state);
    ImGui::DockSpaceOverViewport(0U, ImGui::GetMainViewport());
    draw_project_window(&reload_requested, shell_state);
    draw_runtime_window(*shell_state);
    draw_constraints_window(shell_state);
    draw_timeline_window(shell_state);
    draw_hierarchy_window(shell_state);
    draw_viewport_window(shell_state);
    draw_inspector_window(shell_state);

    if (reload_requested) {
        reload_project(shell_state);
    }

    ImGui::Render();
    int framebuffer_width = 0;
    int framebuffer_height = 0;
    glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
    glViewport(0, 0, framebuffer_width, framebuffer_height);
    glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

int run_headless_smoke(const Options& options) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = ImVec2(1440.0f, 900.0f);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;

    apply_editor_theme();
    unsigned char* font_pixels = nullptr;
    int font_width = 0;
    int font_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);

    ShellState shell_state;
    shell_state.project_path = options.project_path;
    if (!reload_project(&shell_state)) {
        std::cerr << shell_state.error_message;
        ImGui::DestroyContext();
        return 1;
    }

    const auto spine_index = shell_state.load_result.skeleton_data->find_bone_index("spine");
    if (!spine_index.has_value()) {
        std::cerr << "Timeline smoke validation requires the spine bone.\n";
        ImGui::DestroyContext();
        return 1;
    }

    if (!set_selected_animation(&shell_state, "attack", "Smoke", false, true)) {
        std::cerr << "Animation selection smoke validation failed for attack.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.selected_animation_name != "attack") {
        std::cerr << "Animation selection did not update the shell timeline state.\n";
        ImGui::DestroyContext();
        return 1;
    }

    if (const auto arm_index = shell_state.load_result.skeleton_data->find_bone_index("arm_l")) {
        if (!scrub_timeline_time(&shell_state, 0.2, "Smoke", false)) {
            std::cerr << "Timeline scrub smoke validation failed for attack at t=0.2.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const double arm_rotation =
            shell_state.preview_skeleton->bone_poses()[*arm_index].local_pose.rotation;
        if (std::abs(arm_rotation - 60.0) > 1e-3) {
            std::cerr << "Timeline scrub did not update the preview arm rotation at t=0.2.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto smoke_layout = build_viewport_layout(
            shell_state,
            ImVec2(0.0f, 0.0f),
            ImVec2(1280.0f, 720.0f));
        if (!smoke_layout.has_value()) {
            std::cerr << "Failed to build a viewport layout for headless smoke validation.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto picked_bone = pick_bone_at_position(
            *smoke_layout,
            smoke_layout->bones[*arm_index].screen_position);
        if (!picked_bone.has_value() || *picked_bone != *arm_index) {
            std::cerr << "Viewport selection smoke validation failed for bone arm_l.\n";
            ImGui::DestroyContext();
            return 1;
        }

        select_bone(&shell_state, *picked_bone, "Viewport", false);
        if (!shell_state.selected_bone_index.has_value() ||
            *shell_state.selected_bone_index != *arm_index) {
            std::cerr << "Viewport selection did not update the inspector selection state.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (const auto spine_index = shell_state.load_result.skeleton_data->find_bone_index("spine")) {
            select_bone(&shell_state, *spine_index, "Hierarchy", false);
            if (!shell_state.selected_bone_index.has_value() ||
                *shell_state.selected_bone_index != *spine_index) {
                std::cerr << "Hierarchy selection did not update the inspector selection state.\n";
                ImGui::DestroyContext();
                return 1;
            }
        }
    }

    if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
        std::cerr << "Animation selection smoke validation failed for idle.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const marrow::runtime::AnimationData* idle_animation = selected_animation(shell_state);
    if (idle_animation == nullptr) {
        std::cerr << "Timeline smoke validation could not resolve the idle animation.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const std::vector<TimelineTrackRow> idle_tracks =
        build_timeline_tracks(*shell_state.load_result.skeleton_data, *idle_animation);
    if (idle_tracks.empty()) {
        std::cerr << "Timeline panel did not expose keyed tracks for the idle animation.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto draw_order_track = std::find_if(
        idle_tracks.begin(),
        idle_tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == "global:draw-order"; });
    if (draw_order_track == idle_tracks.end()) {
        std::cerr << "Timeline smoke validation could not find the global draw-order track.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto event_track = std::find_if(
        idle_tracks.begin(),
        idle_tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == "global:events"; });
    if (event_track == idle_tracks.end()) {
        std::cerr << "Timeline smoke validation could not find the global event track.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const std::string expected_spine_track_id = "bone:" + std::to_string(*spine_index) + ":Rotate";
    const auto spine_track = std::find_if(
        idle_tracks.begin(),
        idle_tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == expected_spine_track_id; });
    if (spine_track == idle_tracks.end()) {
        std::cerr << "Timeline smoke validation could not find the spine rotate track.\n";
        ImGui::DestroyContext();
        return 1;
    }

    if (!focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
        std::cerr << "Timeline track focus did not scrub the idle clip.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.selected_bone_index != spine_index) {
        std::cerr << "Timeline track focus did not synchronize bone selection.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.load_result.project->transform_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any transform timeline edits.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.load_result.project->mesh_deform_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any mesh deform timeline edits.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.load_result.project->draw_order_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any draw-order timeline edits.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.load_result.project->event_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any event timeline edits.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (std::abs(shell_state.preview_skeleton->bone_poses()[*spine_index].local_pose.rotation - 8.0) >
        1e-3) {
        std::cerr << "Timeline track focus did not apply the project-authored spine rotation.\n";
        ImGui::DestroyContext();
        return 1;
    }

    if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
        const auto spark_fx_slot_index =
            shell_state.load_result.skeleton_data->find_slot_index("spark_fx");
        if (!spark_fx_slot_index.has_value() ||
            !focus_timeline_track(&shell_state, *draw_order_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline draw-order focus did not scrub the idle clip.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const auto body_position =
            draw_order_position(*shell_state.preview_skeleton, *body_slot_index);
        const auto spark_fx_position =
            draw_order_position(*shell_state.preview_skeleton, *spark_fx_slot_index);
        if (!body_position.has_value() || !spark_fx_position.has_value() ||
            *body_position != 0U || *spark_fx_position != 2U) {
            std::cerr << "Timeline draw-order focus did not apply the project-authored slot order.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    if (!focus_timeline_track(&shell_state, *event_track, 0.25, "Smoke", false) ||
        shell_state.preview_events.size() != 2U ||
        shell_state.preview_events[0].name != "footstep" ||
        shell_state.preview_events[0].int_value != 7 ||
        shell_state.preview_events[0].string_value != "editor_left" ||
        shell_state.preview_events[1].name != "dust_vfx" ||
        std::abs(shell_state.preview_events[1].float_value - 0.9) > 1e-3 ||
        shell_state.preview_events[1].string_value != "editor_dust") {
        std::cerr << "Timeline event focus did not expose the authored preview events.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (!focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
        std::cerr << "Timeline smoke could not restore the spine rotate focus.\n";
        ImGui::DestroyContext();
        return 1;
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto edit_index = ensure_transform_timeline_edit_index(&shell_state, *spine_track);
        if (!edit_index.has_value()) {
            std::cerr << "Timeline editor smoke could not materialize the spine rotate edit.\n";
            ImGui::DestroyContext();
            return 1;
        }

        auto& rotate_edit =
            shell_state.load_result.project->transform_timeline_edits[*edit_index];
        if (rotate_edit.keyframes.size() != 3) {
            std::cerr << "Timeline editor smoke expected the fixture rotate edit to start with 3 keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        rotate_edit.keyframes[1].angle = 9.0;
        rotate_edit.keyframes[1].interpolation =
            marrow::runtime::Interpolation::linear();
        marrow::editor::TransformKeyframeEdit new_key;
        new_key.time = 0.75;
        new_key.angle = 12.0;
        new_key.interpolation = marrow::runtime::Interpolation::stepped();
        rotate_edit.keyframes.insert(rotate_edit.keyframes.begin() + 2, std::move(new_key));

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = true;

        if (!focus_timeline_track(&shell_state, *spine_track, 0.625, "Smoke", false) ||
            std::abs(shell_state.preview_skeleton->bone_poses()[*spine_index].local_pose.rotation - 10.5) >
                1e-3) {
            std::cerr << "Timeline editor smoke did not apply edited linear interpolation.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!scrub_timeline_time(&shell_state, 0.875, "Smoke", false) ||
            std::abs(shell_state.preview_skeleton->bone_poses()[*spine_index].local_pose.rotation - 12.0) >
                1e-3) {
            std::cerr << "Timeline editor smoke did not apply the inserted stepped key.\n";
            ImGui::DestroyContext();
            return 1;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            ImGui::DestroyContext();
            return 1;
        }
        const auto* exported_idle = exported_skeleton.skeleton_data->find_animation("idle");
        const auto exported_spine_index =
            exported_skeleton.skeleton_data->find_bone_index("spine");
        if (exported_idle == nullptr || !exported_spine_index.has_value()) {
            std::cerr << "Exported shell smoke skeleton lost the idle spine rotate track.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const auto* exported_rotate =
            exported_idle->find_rotate_timeline(*exported_spine_index);
        if (exported_rotate == nullptr || exported_rotate->keyframes.size() != 4 ||
            exported_rotate->keyframes[0].interpolation.kind() !=
                marrow::runtime::InterpolationKind::CubicBezier ||
            exported_rotate->keyframes[1].interpolation.kind() !=
                marrow::runtime::InterpolationKind::Linear ||
            exported_rotate->keyframes[2].interpolation.kind() !=
                marrow::runtime::InterpolationKind::Stepped ||
            std::abs(exported_rotate->keyframes[1].angle - 9.0) > 1e-3 ||
            std::abs(exported_rotate->keyframes[2].angle - 12.0) > 1e-3) {
            std::cerr << "Timeline editor smoke export did not round-trip the edited rotate curve.\n";
            ImGui::DestroyContext();
            return 1;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = false;
        if (!focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline smoke failed to restore the original fixture edit.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto edit_index = ensure_draw_order_timeline_edit_index(&shell_state, *draw_order_track);
        if (!edit_index.has_value()) {
            std::cerr << "Timeline editor smoke could not materialize the draw-order edit.\n";
            ImGui::DestroyContext();
            return 1;
        }

        auto& draw_order_edit =
            shell_state.load_result.project->draw_order_timeline_edits[*edit_index];
        if (draw_order_edit.keyframes.size() != 3U) {
            std::cerr << "Timeline editor smoke expected the draw-order edit to start with 3 keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        draw_order_edit.keyframes[1].slot_names = {
            "body", "spark_fx", "arm_l", "fx_mask", "spawn_anchor", "hurtbox", "guide"};
        marrow::editor::DrawOrderKeyframeEdit new_draw_order_key;
        new_draw_order_key.time = 0.75;
        new_draw_order_key.slot_names = {
            "spark_fx", "body", "arm_l", "fx_mask", "spawn_anchor", "hurtbox", "guide"};
        draw_order_edit.keyframes.insert(
            draw_order_edit.keyframes.begin() + 2,
            std::move(new_draw_order_key));

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = true;

        const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body");
        const auto spark_fx_slot_index =
            shell_state.load_result.skeleton_data->find_slot_index("spark_fx");
        if (!body_slot_index.has_value() || !spark_fx_slot_index.has_value()) {
            std::cerr << "Timeline draw-order smoke could not resolve the edited slot indices.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!focus_timeline_track(&shell_state, *draw_order_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline draw-order smoke could not refocus the edited draw-order track.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const auto edited_body_position =
            draw_order_position(*shell_state.preview_skeleton, *body_slot_index);
        const auto edited_spark_fx_position =
            draw_order_position(*shell_state.preview_skeleton, *spark_fx_slot_index);
        if (!edited_body_position.has_value() || !edited_spark_fx_position.has_value() ||
            *edited_body_position != 0U || *edited_spark_fx_position != 1U) {
            std::cerr << "Timeline editor smoke did not apply the edited draw-order key.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!scrub_timeline_time(&shell_state, 0.875, "Smoke", false)) {
            std::cerr << "Timeline draw-order smoke could not scrub to the inserted key.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const auto inserted_body_position =
            draw_order_position(*shell_state.preview_skeleton, *body_slot_index);
        const auto inserted_spark_fx_position =
            draw_order_position(*shell_state.preview_skeleton, *spark_fx_slot_index);
        if (!inserted_body_position.has_value() || !inserted_spark_fx_position.has_value() ||
            *inserted_body_position != 1U || *inserted_spark_fx_position != 0U) {
            std::cerr << "Timeline editor smoke did not apply the inserted draw-order key.\n";
            ImGui::DestroyContext();
            return 1;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_draw_order_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_draw_order_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            ImGui::DestroyContext();
            return 1;
        }
        const auto* exported_idle = exported_skeleton.skeleton_data->find_animation("idle");
        const auto* exported_draw_order =
            exported_idle != nullptr ? exported_idle->find_draw_order_timeline() : nullptr;
        if (exported_draw_order == nullptr || exported_draw_order->keyframes.size() != 4U ||
            exported_draw_order->keyframes[1].slot_indices.size() < 3U ||
            exported_skeleton.skeleton_data->slots()[exported_draw_order->keyframes[1].slot_indices[0]].name !=
                "body" ||
            exported_skeleton.skeleton_data->slots()[exported_draw_order->keyframes[1].slot_indices[1]].name !=
                "spark_fx" ||
            exported_skeleton.skeleton_data->slots()[exported_draw_order->keyframes[2].slot_indices[0]].name !=
                "spark_fx") {
            std::cerr << "Timeline editor smoke export did not round-trip the edited draw-order keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state) ||
            !focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline smoke failed to restore the original draw-order fixture edit.\n";
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = false;
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto edit_index = ensure_event_timeline_edit_index(&shell_state, *event_track);
        if (!edit_index.has_value()) {
            std::cerr << "Timeline editor smoke could not materialize the event edit.\n";
            ImGui::DestroyContext();
            return 1;
        }

        auto& event_edit =
            shell_state.load_result.project->event_timeline_edits[*edit_index];
        if (event_edit.keyframes.size() != 3U) {
            std::cerr << "Timeline editor smoke expected the event edit to start with 3 keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        event_edit.keyframes[0].int_value = 11;
        marrow::editor::EventKeyframeEdit new_event_key;
        new_event_key.time = 0.9;
        new_event_key.event_name = "dust_vfx";
        new_event_key.float_value = 0.55;
        new_event_key.string_value = "editor_trail";
        event_edit.keyframes.push_back(std::move(new_event_key));

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = true;

        if (!focus_timeline_track(&shell_state, *event_track, 0.9, "Smoke", false) ||
            shell_state.preview_events.size() != 4U ||
            shell_state.preview_events.front().int_value != 11 ||
            shell_state.preview_events.back().name != "dust_vfx" ||
            std::abs(shell_state.preview_events.back().float_value - 0.55) > 1e-3 ||
            shell_state.preview_events.back().string_value != "editor_trail") {
            std::cerr << "Timeline editor smoke did not apply the edited event payloads.\n";
            ImGui::DestroyContext();
            return 1;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_event_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_event_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            ImGui::DestroyContext();
            return 1;
        }
        const auto* exported_idle = exported_skeleton.skeleton_data->find_animation("idle");
        const auto* exported_events =
            exported_idle != nullptr ? exported_idle->find_event_timeline() : nullptr;
        if (exported_events == nullptr || exported_events->keyframes.size() != 4U ||
            exported_events->keyframes.front().int_value != std::optional<int>(11) ||
            exported_events->keyframes.back().event_index >=
                exported_skeleton.skeleton_data->events().size() ||
            exported_skeleton.skeleton_data
                    ->events()[exported_events->keyframes.back().event_index]
                    .name != "dust_vfx" ||
            exported_events->keyframes.back().string_value !=
                std::optional<std::string>("editor_trail")) {
            std::cerr << "Timeline editor smoke export did not round-trip the edited event keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state) ||
            !focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline smoke failed to restore the original event fixture edit.\n";
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = false;
    }

    if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
        const std::string deform_track_id =
            "slot:" + std::to_string(*body_slot_index) + ":deform:body_mesh";
        const auto deform_track = std::find_if(
            idle_tracks.begin(),
            idle_tracks.end(),
            [&](const TimelineTrackRow& track) { return track.id == deform_track_id; });
        if (deform_track == idle_tracks.end()) {
            std::cerr << "Timeline smoke validation could not find the body mesh deform track.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!focus_timeline_track(&shell_state, *deform_track, 0.5, "Smoke", false) ||
            shell_state.selected_slot_index != body_slot_index) {
            std::cerr << "Timeline deform track focus did not synchronize slot selection.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::vector<double>* fixture_offsets =
            shell_state.preview_skeleton->current_mesh_vertex_offsets(*body_slot_index);
        if (fixture_offsets == nullptr || fixture_offsets->size() != 8U ||
            std::abs((*fixture_offsets)[2] - 12.0) > 1e-3 ||
            std::abs((*fixture_offsets)[3] + 8.0) > 1e-3 ||
            std::abs((*fixture_offsets)[4] - 16.0) > 1e-3 ||
            std::abs((*fixture_offsets)[5] - 20.0) > 1e-3) {
            std::cerr << "Timeline deform track focus did not apply the project-authored FFD key.\n";
            ImGui::DestroyContext();
            return 1;
        }

        {
            marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
            const auto edit_index = ensure_mesh_deform_timeline_edit_index(&shell_state, *deform_track);
            if (!edit_index.has_value()) {
                std::cerr << "Timeline editor smoke could not materialize the deform edit.\n";
                ImGui::DestroyContext();
                return 1;
            }

            auto& deform_edit =
                shell_state.load_result.project->mesh_deform_timeline_edits[*edit_index];
            if (deform_edit.keyframes.size() != 3U) {
                std::cerr << "Timeline editor smoke expected the deform edit to start with 3 keys.\n";
                ImGui::DestroyContext();
                return 1;
            }

            deform_edit.keyframes[1].vertex_offsets = {0.0, 0.0, 14.0, -10.0, 18.0, 24.0, -6.0, 12.0};
            marrow::editor::DeformKeyframeEdit new_deform_key;
            new_deform_key.time = 0.75;
            new_deform_key.vertex_offsets = {0.0, 0.0, 8.0, -6.0, 10.0, 14.0, -3.0, 7.0};
            new_deform_key.interpolation = marrow::runtime::Interpolation::stepped();
            deform_edit.keyframes.insert(
                deform_edit.keyframes.begin() + 2,
                std::move(new_deform_key));

            if (!rebuild_project_runtime(&shell_state)) {
                std::cerr << shell_state.error_message;
                ImGui::DestroyContext();
                return 1;
            }
            shell_state.project_dirty = true;

            if (!focus_timeline_track(&shell_state, *deform_track, 0.5, "Smoke", false)) {
                std::cerr << "Timeline deform smoke could not refocus the edited deform track.\n";
                ImGui::DestroyContext();
                return 1;
            }
            const std::vector<double>* edited_mid_offsets =
                shell_state.preview_skeleton->current_mesh_vertex_offsets(*body_slot_index);
            if (edited_mid_offsets == nullptr || edited_mid_offsets->size() != 8U ||
                std::abs((*edited_mid_offsets)[2] - 14.0) > 1e-3 ||
                std::abs((*edited_mid_offsets)[3] + 10.0) > 1e-3 ||
                std::abs((*edited_mid_offsets)[4] - 18.0) > 1e-3 ||
                std::abs((*edited_mid_offsets)[5] - 24.0) > 1e-3) {
                std::cerr << "Timeline editor smoke did not apply edited deform offsets.\n";
                ImGui::DestroyContext();
                return 1;
            }
            if (!scrub_timeline_time(&shell_state, 0.875, "Smoke", false)) {
                std::cerr << "Timeline deform smoke could not scrub to the inserted key.\n";
                ImGui::DestroyContext();
                return 1;
            }
            const std::vector<double>* inserted_offsets =
                shell_state.preview_skeleton->current_mesh_vertex_offsets(*body_slot_index);
            if (inserted_offsets == nullptr || inserted_offsets->size() != 8U ||
                std::abs((*inserted_offsets)[2] - 8.0) > 1e-3 ||
                std::abs((*inserted_offsets)[3] + 6.0) > 1e-3 ||
                std::abs((*inserted_offsets)[4] - 10.0) > 1e-3 ||
                std::abs((*inserted_offsets)[5] - 14.0) > 1e-3) {
                std::cerr << "Timeline editor smoke did not apply the inserted deform key.\n";
                ImGui::DestroyContext();
                return 1;
            }

            marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
            temp_project.source_path = "/tmp/marrow_editor_shell_deform_smoke.marrow";
            materialize_temp_project_runtime_assets(shell_state, &temp_project);

            const auto save_result =
                marrow::editor::save_project(temp_project, temp_project.source_path);
            if (!save_result) {
                std::cerr << save_result.error->format() << '\n';
                ImGui::DestroyContext();
                return 1;
            }
            const auto export_result = marrow::editor::export_runtime_skeleton(
                *save_result.project,
                *shell_state.load_result.base_skeleton_document,
                "/tmp/marrow_editor_shell_deform_smoke.mskl");
            if (!export_result) {
                std::cerr << export_result.error->format() << '\n';
                ImGui::DestroyContext();
                return 1;
            }

            const auto exported_skeleton =
                marrow::runtime::load_skeleton_data(export_result.path);
            if (!exported_skeleton) {
                std::cerr << exported_skeleton.error->format();
                ImGui::DestroyContext();
                return 1;
            }

            const auto exported_body_slot_index =
                exported_skeleton.skeleton_data->find_slot_index("body");
            const auto* exported_idle =
                exported_skeleton.skeleton_data->find_animation("idle");
            if (!exported_body_slot_index.has_value() || exported_idle == nullptr) {
                std::cerr << "Exported shell smoke skeleton lost the idle deform track.\n";
                ImGui::DestroyContext();
                return 1;
            }
            const auto* exported_deform =
                exported_idle->find_deform_timeline(*exported_body_slot_index, "body_mesh");
            if (exported_deform == nullptr || exported_deform->keyframes.size() != 4U ||
                std::abs(exported_deform->keyframes[1].vertex_offsets[2] - 14.0) > 1e-3 ||
                std::abs(exported_deform->keyframes[2].vertex_offsets[2] - 8.0) > 1e-3 ||
                exported_deform->keyframes[2].interpolation.kind() !=
                    marrow::runtime::InterpolationKind::Stepped) {
                std::cerr << "Timeline editor smoke export did not round-trip the edited deform keys.\n";
                ImGui::DestroyContext();
                return 1;
            }

            marrow::runtime::Skeleton exported_preview(exported_skeleton.skeleton_data);
            if (!exported_preview.set_skin("warrior")) {
                std::cerr << "Exported deform smoke could not activate the warrior skin.\n";
                ImGui::DestroyContext();
                return 1;
            }
            exported_preview.apply_animation(*exported_idle, 0.875);
            const std::vector<double>* exported_offsets =
                exported_preview.current_mesh_vertex_offsets(*exported_body_slot_index);
            if (exported_offsets == nullptr || exported_offsets->size() != 8U ||
                std::abs((*exported_offsets)[2] - 8.0) > 1e-3 ||
                std::abs((*exported_offsets)[5] - 14.0) > 1e-3) {
                std::cerr << "Runtime playback did not preserve the exported deform offsets.\n";
                ImGui::DestroyContext();
                return 1;
            }

            *shell_state.load_result.project = std::move(previous_project);
            if (!rebuild_project_runtime(&shell_state) ||
                !focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
                std::cerr << "Timeline smoke failed to restore the original deform fixture edit.\n";
                ImGui::DestroyContext();
                return 1;
            }
            shell_state.project_dirty = false;
        }

        const auto* animated_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (animated_attachment == nullptr || animated_attachment->name != "warrior_body") {
            std::cerr << "Timeline scrub did not synchronize the animated body attachment.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
        select_slot(&shell_state, *body_slot_index, "Smoke", false);
        if (!shell_state.selected_slot_index.has_value() ||
            *shell_state.selected_slot_index != *body_slot_index) {
            std::cerr << "Slot selection did not update the inspector slot state.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!shell_state.selected_attachment.has_value() ||
            shell_state.selected_attachment->attachment_name != "warrior_body") {
            std::cerr << "Slot selection did not resolve the animated body attachment at t=0.5.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto selected_attachment_reference = resolve_attachment_reference(
            *shell_state.load_result.skeleton_data,
            *shell_state.selected_attachment);
        if (!selected_attachment_reference.has_value()) {
            std::cerr << "Slot selection did not resolve the selected body attachment reference.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::vector<MeshWeightVertexRow> weight_rows = build_mesh_weight_rows(
            *shell_state.load_result.skeleton_data,
            *selected_attachment_reference->attachment);
        if (weight_rows.size() != 4U ||
            weight_rows[1].influences.size() != 2U ||
            weight_rows[1].influences[0].bone_name != "spine" ||
            std::abs(weight_rows[1].influences[0].weight - 0.75) > 1e-3 ||
            weight_rows[1].influences[1].bone_name != "arm_l" ||
            std::abs(weight_rows[1].influences[1].weight - 0.25) > 1e-3) {
            std::cerr << "Mesh weight inspector data did not expose the expected weighted influences.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto warrior_skin_index =
            shell_state.load_result.skeleton_data->find_skin_index("warrior");
        if (!warrior_skin_index.has_value() ||
            !set_preview_skin_enabled(&shell_state, *warrior_skin_index, true, false)) {
            std::cerr << "Failed to enable the warrior preview skin in smoke validation.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto* warrior_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (warrior_attachment == nullptr || warrior_attachment->name != "warrior_body" ||
            !warrior_attachment->linked_mesh.has_value()) {
            std::cerr << "Skin preview did not activate the warrior linked mesh attachment.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto mage_skin_index =
            shell_state.load_result.skeleton_data->find_skin_index("mage");
        if (!mage_skin_index.has_value()) {
            std::cerr << "Mage skin is missing from the sample project.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const AttachmentSelection mage_attachment_selection{
            *body_slot_index,
            *mage_skin_index,
            "mage_body"};
        const auto mage_attachment_reference = resolve_attachment_reference(
            *shell_state.load_result.skeleton_data,
            mage_attachment_selection);
        if (!mage_attachment_reference.has_value() ||
            !mage_attachment_reference->attachment->linked_mesh.has_value()) {
            std::cerr << "Attachment inspector smoke could not resolve the mage linked mesh.\n";
            ImGui::DestroyContext();
            return 1;
        }

        select_attachment(&shell_state, mage_attachment_selection, "Smoke", false);
        if (!apply_attachment_selection_to_preview_slot(
                &shell_state,
                mage_attachment_selection,
                "Smoke",
                false)) {
            std::cerr << "Attachment preview override failed for mage_body.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto* mage_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (mage_attachment == nullptr || mage_attachment->name != "mage_body" ||
            !mage_attachment->linked_mesh.has_value() ||
            mage_attachment->linked_mesh->parent_attachment != "body_mesh") {
            std::cerr << "Attachment override did not expose the linked mesh relationship.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!reset_preview_slot_to_skin_selection(&shell_state, *body_slot_index, "Smoke", false)) {
            std::cerr << "Failed to reset the body slot to the active skin preview.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto* restored_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (restored_attachment == nullptr || restored_attachment->name != "warrior_body") {
            std::cerr << "Resetting the slot preview did not restore the warrior skin state.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    if (!set_selected_animation(&shell_state, "attack", "Smoke", false, true)) {
        std::cerr << "State preview smoke could not select the attack animation.\n";
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.preview_queue_enabled = true;
    shell_state.preview_queued_animation_name = "aim";
    shell_state.preview_queue_delay = 0.0;
    shell_state.preview_use_custom_mix_duration = true;
    shell_state.preview_custom_mix_duration = 0.1;
    shell_state.preview_reverse = false;
    if (!scrub_timeline_time(&shell_state, 0.45, "Smoke", false)) {
        std::cerr << "State preview smoke could not scrub the queued attack->aim transition.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (std::abs(timeline_preview_duration(shell_state) - 0.9) > 1e-6) {
        std::cerr << "State preview smoke did not compute the queued preview duration.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (const auto arm_index = shell_state.load_result.skeleton_data->find_bone_index("arm_l")) {
        const double mixed_rotation =
            shell_state.preview_skeleton->bone_poses()[*arm_index].local_pose.rotation;
        if (std::abs(mixed_rotation - 45.0) > 1e-3) {
            std::cerr << "State preview smoke did not apply the queued mix pose.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    shell_state.preview_queue_enabled = false;
    shell_state.preview_use_custom_mix_duration = false;
    shell_state.preview_reverse = true;
    if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true) ||
        !scrub_timeline_time(&shell_state, 0.35, "Smoke", false)) {
        std::cerr << "State preview smoke could not scrub reverse idle playback.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (std::abs(shell_state.preview_root_motion_total.x + 14.0) > 1e-3 ||
        std::abs(shell_state.preview_root_motion_total.y - 7.0) > 1e-3) {
        std::cerr << "State preview smoke did not surface reverse root-motion playback.\n";
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.preview_reverse = false;

    const auto require_smoke_near =
        [](double actual, double expected, double epsilon, std::string_view label) {
            if (std::abs(actual - expected) <= epsilon) {
                return true;
            }

            std::cerr << label << " expected " << expected << " but was " << actual << ".\n";
            return false;
        };

    shell_state.preview_skin_names = {"default"};
    shell_state.preview_slot_overrides.assign(
        shell_state.preview_slot_overrides.size(), std::nullopt);
    if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
        std::cerr << "Constraint smoke could not restore the idle preview state.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto ik_tip_index = shell_state.load_result.skeleton_data->find_bone_index("ik_tip");
    const auto ik_target_index = shell_state.load_result.skeleton_data->find_bone_index("ik_target");
    const auto path_a_index = shell_state.load_result.skeleton_data->find_bone_index("path_a");
    const auto path_b_index = shell_state.load_result.skeleton_data->find_bone_index("path_b");
    const auto path_c_index = shell_state.load_result.skeleton_data->find_bone_index("path_c");
    const auto transform_source_index =
        shell_state.load_result.skeleton_data->find_bone_index("transform_source");
    const auto transform_target_index =
        shell_state.load_result.skeleton_data->find_bone_index("transform_target");
    const auto pivot_index = shell_state.load_result.skeleton_data->find_bone_index("pivot");
    const auto ribbon_tip_index =
        shell_state.load_result.skeleton_data->find_bone_index("ribbon_tip");
    if (!ik_tip_index.has_value() || !ik_target_index.has_value() || !path_a_index.has_value() ||
        !path_b_index.has_value() || !path_c_index.has_value() ||
        !transform_source_index.has_value() || !transform_target_index.has_value() ||
        !pivot_index.has_value() || !ribbon_tip_index.has_value()) {
        std::cerr << "Constraint smoke fixture lost the helper bones needed for authoring validation.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto* authored_ik = find_named_constraint(
        shell_state.load_result.skeleton_data->ik_constraints(),
        "editor_arm_reach");
    const auto* authored_path = find_named_constraint(
        shell_state.load_result.skeleton_data->path_constraints(),
        "editor_guide_follow");
    const auto* authored_transform = find_named_constraint(
        shell_state.load_result.skeleton_data->transform_constraints(),
        "editor_transform_follow");
    const auto* authored_physics = find_named_constraint(
        shell_state.load_result.skeleton_data->physics_constraints(),
        "editor_ribbon_secondary");
    if (shell_state.load_result.project->ik_constraint_edits.size() != 1U ||
        shell_state.load_result.project->path_constraint_edits.size() != 1U ||
        shell_state.load_result.project->transform_constraint_edits.size() != 1U ||
        shell_state.load_result.project->physics_constraint_edits.size() != 1U ||
        authored_ik == nullptr || authored_path == nullptr ||
        authored_transform == nullptr || authored_physics == nullptr) {
        std::cerr << "Constraint smoke fixture did not expose the expected authored constraint edits.\n";
        ImGui::DestroyContext();
        return 1;
    }

    select_constraint(
        &shell_state,
        ConstraintEditKind::Transform,
        "editor_transform_follow",
        "Smoke",
        false);
    if (!shell_state.selected_constraint.has_value() ||
        shell_state.selected_constraint->kind != ConstraintEditKind::Transform ||
        shell_state.selected_constraint->name != "editor_transform_follow") {
        std::cerr << "Constraint selection smoke did not preserve the chosen transform constraint.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto& ik_tip_world =
        shell_state.preview_skeleton->bone_world_transforms()[*ik_tip_index];
    const auto& ik_target_world =
        shell_state.preview_skeleton->bone_world_transforms()[*ik_target_index];
    if (!require_smoke_near(
            ik_tip_world.world_x,
            ik_target_world.world_x,
            1e-3,
            "constraint preview IK tip x") ||
        !require_smoke_near(
            ik_tip_world.world_y,
            ik_target_world.world_y,
            1e-3,
            "constraint preview IK tip y")) {
        std::cerr << "Constraint preview did not solve the authored IK reach target.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto& path_a_world =
        shell_state.preview_skeleton->bone_world_transforms()[*path_a_index];
    const auto& path_b_world =
        shell_state.preview_skeleton->bone_world_transforms()[*path_b_index];
    const auto& path_c_world =
        shell_state.preview_skeleton->bone_world_transforms()[*path_c_index];
    if (!require_smoke_near(path_a_world.world_x, 20.0, 1e-3, "constraint preview path_a x") ||
        !require_smoke_near(path_a_world.world_y, 0.0, 1e-3, "constraint preview path_a y") ||
        !require_smoke_near(path_b_world.world_x, 80.0, 1e-3, "constraint preview path_b x") ||
        !require_smoke_near(path_b_world.world_y, 0.0, 1e-3, "constraint preview path_b y") ||
        !require_smoke_near(path_c_world.world_x, 100.0, 1e-3, "constraint preview path_c x") ||
        !require_smoke_near(path_c_world.world_y, 40.0, 1e-3, "constraint preview path_c y")) {
        std::cerr << "Constraint preview did not place the path chain on the authored guide.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const double expected_transform_rotation = 12.5;
    const double expected_transform_x = 215.0;
    const double expected_transform_y = 7.5;
    const double expected_transform_scale_x = 1.5;
    const double expected_transform_scale_y = 0.7;
    const double expected_transform_shear_x = 9.75;
    const double expected_transform_shear_y = -4.5;
    const double kPi = 3.14159265358979323846;
    const double transform_x_radians =
        (expected_transform_rotation + expected_transform_shear_x) * kPi / 180.0;
    const double transform_y_radians =
        (expected_transform_rotation + 90.0 + expected_transform_shear_y) * kPi / 180.0;
    const auto& transform_target_world =
        shell_state.preview_skeleton->bone_world_transforms()[*transform_target_index];
    if (!require_smoke_near(
            transform_target_world.world_x,
            expected_transform_x,
            1e-3,
            "constraint preview transform target x") ||
        !require_smoke_near(
            transform_target_world.world_y,
            expected_transform_y,
            1e-3,
            "constraint preview transform target y") ||
        !require_smoke_near(
            transform_target_world.a,
            std::cos(transform_x_radians) * expected_transform_scale_x,
            1e-3,
            "constraint preview transform target axis a") ||
        !require_smoke_near(
            transform_target_world.b,
            std::cos(transform_y_radians) * expected_transform_scale_y,
            1e-3,
            "constraint preview transform target axis b") ||
        !require_smoke_near(
            transform_target_world.c,
            std::sin(transform_x_radians) * expected_transform_scale_x,
            1e-3,
            "constraint preview transform target axis c") ||
        !require_smoke_near(
            transform_target_world.d,
            std::sin(transform_y_radians) * expected_transform_scale_y,
            1e-3,
            "constraint preview transform target axis d")) {
        std::cerr << "Constraint preview did not apply the authored transform constraint.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto& setup_ribbon_tip =
        shell_state.preview_skeleton->bone_world_transforms()[*ribbon_tip_index];
    if (!require_smoke_near(
            setup_ribbon_tip.world_x,
            230.0,
            5.0,
            "constraint preview setup ribbon tip x") ||
        !require_smoke_near(
            setup_ribbon_tip.world_y,
            -120.0,
            5.0,
            "constraint preview setup ribbon tip y")) {
        std::cerr << "Constraint preview lost the helper ribbon setup pose.\n";
        ImGui::DestroyContext();
        return 1;
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto ik_edit_index =
            ensure_ik_constraint_edit_index(&shell_state, "editor_arm_reach");
        const auto path_edit_index =
            ensure_path_constraint_edit_index(&shell_state, "editor_guide_follow");
        const auto transform_edit_index =
            ensure_transform_constraint_edit_index(&shell_state, "editor_transform_follow");
        const auto physics_edit_index =
            ensure_physics_constraint_edit_index(&shell_state, "editor_ribbon_secondary");
        if (!ik_edit_index.has_value() || !path_edit_index.has_value() ||
            !transform_edit_index.has_value() || !physics_edit_index.has_value()) {
            std::cerr << "Constraint editor smoke could not materialize the authored constraint edits.\n";
            ImGui::DestroyContext();
            return 1;
        }

        shell_state.load_result.project->path_constraint_edits[*path_edit_index].position = 0.0;
        shell_state.load_result.project->transform_constraint_edits[*transform_edit_index]
            .translate_mix = 0.5;
        shell_state.load_result.project->physics_constraint_edits[*physics_edit_index].wind.x =
            18.0;

        marrow::editor::IkConstraintEdit created_ik;
        created_ik.name =
            unique_constraint_name(shell_state, ConstraintEditKind::Ik, "editor_created_ik");
        created_ik.bone_names = {"transform_target"};
        created_ik.target_bone_name = "transform_source";
        created_ik.mix = 0.0;
        created_ik.bend_positive = true;
        shell_state.load_result.project->ik_constraint_edits.push_back(created_ik);
        const std::string created_ik_name = created_ik.name;

        select_constraint(
            &shell_state,
            ConstraintEditKind::Ik,
            created_ik_name,
            "Smoke",
            false);

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = true;

        const auto* created_runtime_ik = find_named_constraint(
            shell_state.load_result.skeleton_data->ik_constraints(),
            created_ik_name);
        const auto* edited_runtime_path = find_named_constraint(
            shell_state.load_result.skeleton_data->path_constraints(),
            "editor_guide_follow");
        const auto* edited_runtime_transform = find_named_constraint(
            shell_state.load_result.skeleton_data->transform_constraints(),
            "editor_transform_follow");
        const auto* edited_runtime_physics = find_named_constraint(
            shell_state.load_result.skeleton_data->physics_constraints(),
            "editor_ribbon_secondary");
        if (created_runtime_ik == nullptr || edited_runtime_path == nullptr ||
            edited_runtime_transform == nullptr || edited_runtime_physics == nullptr ||
            !require_smoke_near(
                edited_runtime_path->position,
                0.0,
                1e-6,
                "edited runtime path position") ||
            !require_smoke_near(
                edited_runtime_transform->translate_mix,
                0.5,
                1e-6,
                "edited runtime transform translate mix") ||
            !require_smoke_near(
                edited_runtime_physics->wind.x,
                18.0,
                1e-6,
                "edited runtime physics wind.x") ||
            !require_smoke_near(created_runtime_ik->mix, 0.0, 1e-6, "created runtime IK mix")) {
            std::cerr << "Constraint editor smoke did not rebuild the edited runtime constraint data.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
            std::cerr << "Constraint editor smoke could not refresh the idle preview after edits.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto& edited_path_a_world =
            shell_state.preview_skeleton->bone_world_transforms()[*path_a_index];
        const auto& edited_path_b_world =
            shell_state.preview_skeleton->bone_world_transforms()[*path_b_index];
        const auto& edited_path_c_world =
            shell_state.preview_skeleton->bone_world_transforms()[*path_c_index];
        if (!require_smoke_near(
                edited_path_a_world.world_x,
                0.0,
                1e-3,
                "edited constraint preview path_a x") ||
            !require_smoke_near(
                edited_path_a_world.world_y,
                0.0,
                1e-3,
                "edited constraint preview path_a y") ||
            !require_smoke_near(
                edited_path_b_world.world_x,
                60.0,
                1e-3,
                "edited constraint preview path_b x") ||
            !require_smoke_near(
                edited_path_b_world.world_y,
                0.0,
                1e-3,
                "edited constraint preview path_b y") ||
            !require_smoke_near(
                edited_path_c_world.world_x,
                100.0,
                1e-3,
                "edited constraint preview path_c x") ||
            !require_smoke_near(
                edited_path_c_world.world_y,
                20.0,
                1e-3,
                "edited constraint preview path_c y")) {
            std::cerr << "Constraint editor smoke did not re-preview the edited path constraint.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto& edited_transform_target_world =
            shell_state.preview_skeleton->bone_world_transforms()[*transform_target_index];
        if (!require_smoke_near(
                edited_transform_target_world.world_x,
                200.0,
                1e-3,
                "edited constraint preview transform target x") ||
            !require_smoke_near(
                edited_transform_target_world.world_y,
                25.0,
                1e-3,
                "edited constraint preview transform target y")) {
            std::cerr << "Constraint editor smoke did not re-preview the edited transform constraint.\n";
            ImGui::DestroyContext();
            return 1;
        }

        shell_state.preview_skeleton->bone_poses()[*pivot_index].local_pose.rotation = 90.0;
        shell_state.preview_skeleton->update_world_transforms();
        const auto lagged_ribbon_tip =
            shell_state.preview_skeleton->bone_world_transforms()[*ribbon_tip_index];
        shell_state.preview_skeleton->update_physics(1.0 / 60.0);
        const auto stepped_ribbon_tip =
            shell_state.preview_skeleton->bone_world_transforms()[*ribbon_tip_index];
        const double preview_physics_motion = std::hypot(
            stepped_ribbon_tip.world_x - lagged_ribbon_tip.world_x,
            stepped_ribbon_tip.world_y - lagged_ribbon_tip.world_y);
        if (preview_physics_motion <= 0.1) {
            std::cerr << "Constraint editor smoke did not advance the preview physics chain.\n";
            ImGui::DestroyContext();
            return 1;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_constraints_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_constraints_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            ImGui::DestroyContext();
            return 1;
        }

        const auto* exported_created_ik = find_named_constraint(
            exported_skeleton.skeleton_data->ik_constraints(),
            created_ik_name);
        const auto* exported_path = find_named_constraint(
            exported_skeleton.skeleton_data->path_constraints(),
            "editor_guide_follow");
        const auto* exported_transform = find_named_constraint(
            exported_skeleton.skeleton_data->transform_constraints(),
            "editor_transform_follow");
        const auto* exported_physics = find_named_constraint(
            exported_skeleton.skeleton_data->physics_constraints(),
            "editor_ribbon_secondary");
        if (exported_created_ik == nullptr || exported_path == nullptr ||
            exported_transform == nullptr || exported_physics == nullptr ||
            exported_created_ik->bone_indices !=
                std::vector<std::size_t>{*transform_target_index} ||
            exported_created_ik->target_bone_index != *transform_source_index ||
            !require_smoke_near(
                exported_path->position,
                0.0,
                1e-6,
                "exported path position") ||
            !require_smoke_near(
                exported_transform->translate_mix,
                0.5,
                1e-6,
                "exported transform translate mix") ||
            !require_smoke_near(
                exported_physics->wind.x,
                18.0,
                1e-6,
                "exported physics wind.x")) {
            std::cerr << "Constraint editor smoke export did not round-trip the edited constraints.\n";
            ImGui::DestroyContext();
            return 1;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state) ||
            !set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
            std::cerr << "Constraint editor smoke failed to restore the original project state.\n";
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = false;
    }

    const int frame_count = options.auto_close_frames.value_or(1);
    for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        advance_timeline_playback(&shell_state, io.DeltaTime);

        bool reload_requested = false;
        draw_project_window(&reload_requested, &shell_state);
        draw_runtime_window(shell_state);
        draw_constraints_window(&shell_state);
        draw_timeline_window(&shell_state);
        draw_hierarchy_window(&shell_state);
        draw_viewport_window(&shell_state);
        draw_inspector_window(&shell_state);
        ImGui::Render();

        if (reload_requested && !reload_project(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
    }

    std::cout << shell_state.status_message << '\n'
              << "Headless editor shell smoke rendered " << frame_count << " frame(s).\n";

    ImGui::DestroyContext();
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    const ParseResult parse_result = parse_arguments(argc, argv);
    if (parse_result.status == ParseStatus::Help) {
        return 0;
    }
    if (parse_result.status != ParseStatus::Ok) {
        return 1;
    }

    const bool smoke_mode = parse_result.options.auto_close_frames.has_value();
#if defined(__APPLE__)
    if (smoke_mode) {
        return run_headless_smoke(parse_result.options);
    }
#endif

    glfwSetErrorCallback(glfw_error_callback);
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
    glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW.\n";
        return 1;
    }
    configure_glfw_for_editor();
    if (smoke_mode) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    const char* glsl_version = "#version 150";
    const float scale = monitor_content_scale();
    GLFWwindow* window = glfwCreateWindow(
        static_cast<int>(1440.0f * scale),
        static_cast<int>(900.0f * scale),
        std::string(marrow::editor::component_name()).c_str(),
        nullptr,
        nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create the Marrow editor shell window.\n";
        glfwTerminate();
        return 1;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(smoke_mode ? 0 : 1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_DpiEnableScaleFonts;
    io.IniFilename = nullptr;

    apply_editor_theme();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ShellState shell_state;
    shell_state.project_path = parse_result.options.project_path;
    reload_project(&shell_state);

    int rendered_frames = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        render_shell_frame(window, &shell_state);
        glfwSwapBuffers(window);

        ++rendered_frames;
        if (parse_result.options.auto_close_frames.has_value() &&
            rendered_frames >= *parse_result.options.auto_close_frames) {
            break;
        }
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
