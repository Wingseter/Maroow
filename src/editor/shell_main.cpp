#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
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

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"

#include "macos_app_focus.hpp"
#include "shell_types.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/module.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/profiler.hpp"

namespace marrow::editor::shell {

// ── Editor Fonts ──
ImFont* g_font_regular = nullptr;
ImFont* g_font_semibold = nullptr;

void print_usage(std::string_view executable_name) {
    std::cout << "Usage: " << executable_name
              << " [project.marrow] [--auto-close <frames>] [--verify-launch-focus]\n"
                 "       "
              << executable_name
              << " --project <project.marrow> [--auto-close <frames>] "
                 "[--verify-launch-focus]\n"
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

        if (argument == "--verify-launch-focus") {
            result.options.verify_launch_focus = true;
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

    if (result.options.verify_launch_focus &&
        result.options.auto_close_frames.has_value()) {
        std::cerr << "--verify-launch-focus cannot be combined with --auto-close.\n";
        print_usage(argv[0]);
        return result;
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
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    glfwWindowHint(GLFW_SAMPLES, 4);
}

#if defined(__APPLE__)
void activate_editor_window_on_launch(GLFWwindow* window) {
    if (!marrow::editor::platform::activate_editor_application()) {
        std::cerr << "Warning: failed to promote the Marrow editor app to a regular "
                     "foreground macOS application.\n";
    }
    glfwFocusWindow(window);
}

bool verify_editor_launch_focus_configuration(GLFWwindow* window) {
    bool success = true;

    if (glfwGetWindowAttrib(window, GLFW_FOCUS_ON_SHOW) != GLFW_TRUE) {
        std::cerr << "Expected GLFW_FOCUS_ON_SHOW to be enabled for the editor "
                     "window.\n";
        success = false;
    }

    if (!marrow::editor::platform::activate_editor_application()) {
        std::cerr << "Expected macOS launch activation to promote the editor to a "
                     "regular foreground application.\n";
        success = false;
    }

    if (!marrow::editor::platform::uses_regular_activation_policy()) {
        std::cerr << "Expected the Marrow editor to use "
                     "NSApplicationActivationPolicyRegular for Cmd+Tab visibility.\n";
        success = false;
    }

    return success;
}

int run_launch_focus_verification() {
    glfwSetErrorCallback(glfw_error_callback);
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
    glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW for launch-focus verification.\n";
        return 1;
    }

    configure_glfw_for_editor();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(640, 480, "Marrow Launch Focus Verification", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Failed to create the Marrow launch-focus verification window.\n";
        glfwTerminate();
        return 1;
    }

    const bool success = verify_editor_launch_focus_configuration(window);
    glfwDestroyWindow(window);
    glfwTerminate();

    if (!success) {
        return 1;
    }

    std::cout << "Verified macOS editor launch focus configuration.\n";
    return 0;
}
#endif

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

RuntimeAssetWatchEntry make_runtime_asset_watch_entry(const std::filesystem::path& path) {
    RuntimeAssetWatchEntry entry;
    entry.path = absolutize_path(path);

    std::error_code error;
    entry.exists = std::filesystem::exists(entry.path, error);
    if (error || !entry.exists) {
        entry.exists = false;
        return entry;
    }

    const auto write_time = std::filesystem::last_write_time(entry.path, error);
    if (!error) {
        entry.write_time = write_time;
    }
    return entry;
}

bool runtime_asset_watch_entry_equal(
    const RuntimeAssetWatchEntry& left,
    const RuntimeAssetWatchEntry& right) {
    return left.path == right.path &&
        left.exists == right.exists &&
        left.write_time == right.write_time;
}

std::vector<std::filesystem::path> current_runtime_asset_paths(const ShellState& state) {
    std::vector<std::filesystem::path> paths;
    if (!state.load_result || state.load_result.project == nullptr) {
        return paths;
    }

    paths.push_back(absolutize_path(state.load_result.project->resolved_skeleton_path()));
    for (const auto& atlas_path : state.load_result.project->resolved_atlas_paths()) {
        paths.push_back(absolutize_path(atlas_path));
    }
    return paths;
}

std::vector<RuntimeAssetWatchEntry> capture_runtime_asset_watch_entries(const ShellState& state) {
    const std::vector<std::filesystem::path> paths = current_runtime_asset_paths(state);
    std::vector<RuntimeAssetWatchEntry> entries;
    entries.reserve(paths.size());
    for (const auto& path : paths) {
        entries.push_back(make_runtime_asset_watch_entry(path));
    }
    return entries;
}

void reset_runtime_asset_watch(ShellState* state) {
    if (state == nullptr) {
        return;
    }

    state->runtime_asset_watch_entries = capture_runtime_asset_watch_entries(*state);
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
    ImVec4* c = style.Colors;

    // ── Surface Hierarchy (Charcoal Studio) ──
    c[ImGuiCol_WindowBg]           = ImVec4(0.102f, 0.114f, 0.137f, 1.0f);  // #1A1D23
    c[ImGuiCol_ChildBg]            = ImVec4(0.133f, 0.149f, 0.180f, 1.0f);  // #22262E
    c[ImGuiCol_PopupBg]            = ImVec4(0.133f, 0.149f, 0.180f, 0.95f);
    c[ImGuiCol_MenuBarBg]          = ImVec4(0.102f, 0.114f, 0.137f, 1.0f);
    c[ImGuiCol_DockingEmptyBg]     = ImVec4(0.063f, 0.075f, 0.098f, 1.0f);  // #101319

    // ── Header ──
    c[ImGuiCol_Header]             = ImVec4(0.165f, 0.184f, 0.227f, 1.0f);  // #2A2F3A
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.200f, 0.224f, 0.275f, 1.0f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.290f, 0.482f, 0.969f, 1.0f);  // #4A7BF7

    // ── Widget ──
    c[ImGuiCol_Button]             = ImVec4(0.200f, 0.227f, 0.278f, 1.0f);  // #333A47
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.239f, 0.271f, 0.337f, 1.0f);  // #3D4556
    c[ImGuiCol_ButtonActive]       = ImVec4(0.290f, 0.482f, 0.969f, 1.0f);
    c[ImGuiCol_FrameBg]            = ImVec4(0.200f, 0.227f, 0.278f, 1.0f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.239f, 0.271f, 0.337f, 1.0f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.290f, 0.482f, 0.969f, 0.67f);
    c[ImGuiCol_CheckMark]          = ImVec4(0.290f, 0.482f, 0.969f, 1.0f);
    c[ImGuiCol_SliderGrab]         = ImVec4(0.290f, 0.482f, 0.969f, 0.80f);
    c[ImGuiCol_SliderGrabActive]   = ImVec4(0.290f, 0.482f, 0.969f, 1.0f);

    // ── Selection ──
    c[ImGuiCol_TextSelectedBg]     = ImVec4(0.290f, 0.482f, 0.969f, 0.35f);

    // ── Tab ──
    c[ImGuiCol_Tab]                = ImVec4(0.133f, 0.149f, 0.180f, 1.0f);  // #22262E
    c[ImGuiCol_TabHovered]         = ImVec4(0.239f, 0.271f, 0.337f, 1.0f);
    c[ImGuiCol_TabActive]          = ImVec4(0.165f, 0.184f, 0.227f, 1.0f);  // #2A2F3A
    c[ImGuiCol_TabUnfocused]       = ImVec4(0.102f, 0.114f, 0.137f, 1.0f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.133f, 0.149f, 0.180f, 1.0f);

    // ── Title Bar ──
    c[ImGuiCol_TitleBg]            = ImVec4(0.102f, 0.114f, 0.137f, 1.0f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.133f, 0.149f, 0.180f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.063f, 0.075f, 0.098f, 0.75f);

    // ── Scrollbar ──
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.102f, 0.114f, 0.137f, 0.53f);
    c[ImGuiCol_ScrollbarGrab]      = ImVec4(0.314f, 0.345f, 0.408f, 1.0f);  // #505868
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.408f, 0.439f, 0.502f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.494f, 0.722f, 1.000f, 1.0f);

    // ── Separator, Border ──
    c[ImGuiCol_Separator]          = ImVec4(0.180f, 0.204f, 0.251f, 1.0f);  // #2E3440
    c[ImGuiCol_SeparatorHovered]   = ImVec4(0.290f, 0.482f, 0.969f, 0.78f);
    c[ImGuiCol_SeparatorActive]    = ImVec4(0.290f, 0.482f, 0.969f, 1.0f);
    c[ImGuiCol_Border]             = ImVec4(0.227f, 0.251f, 0.314f, 0.50f); // #3A4050

    // ── Resize Grip ──
    c[ImGuiCol_ResizeGrip]         = ImVec4(0.290f, 0.482f, 0.969f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.290f, 0.482f, 0.969f, 0.67f);
    c[ImGuiCol_ResizeGripActive]   = ImVec4(0.290f, 0.482f, 0.969f, 0.95f);

    // ── Docking ──
    c[ImGuiCol_DockingPreview]     = ImVec4(0.290f, 0.482f, 0.969f, 0.70f);

    // ── Text ──
    c[ImGuiCol_Text]               = ImVec4(0.878f, 0.894f, 0.925f, 1.0f);  // #E0E4EC
    c[ImGuiCol_TextDisabled]       = ImVec4(0.533f, 0.569f, 0.647f, 1.0f);  // #8891A5

    // ── Table ──
    c[ImGuiCol_TableRowBg]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt]      = ImVec4(0.133f, 0.149f, 0.180f, 0.30f);
    c[ImGuiCol_TableBorderStrong]  = ImVec4(0.227f, 0.251f, 0.314f, 1.0f);
    c[ImGuiCol_TableBorderLight]   = ImVec4(0.180f, 0.204f, 0.251f, 1.0f);

    // ── Style Vars ──
    style.WindowRounding    = 2.0f;
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.TabRounding       = 2.0f;
    style.GrabRounding      = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowBorderSize  = 0.0f;
    style.ChildBorderSize   = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(8.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 8.0f;
}

void load_editor_fonts() {
    ImGuiIO& io = ImGui::GetIO();

    constexpr float kBaseFontSize = 15.0f;

    auto find_font_path = [](const char* filename) -> std::string {
        const std::string candidates[] = {
            std::string("fonts/") + filename,
            std::string("assets/fonts/") + filename,
            std::string("../assets/fonts/") + filename,
        };
        for (const auto& path : candidates) {
            std::error_code ec;
            if (std::filesystem::exists(path, ec)) {
                return path;
            }
        }
        return {};
    };

    const std::string regular_path = find_font_path("Pretendard-Regular.otf");
    const std::string semibold_path = find_font_path("Pretendard-SemiBold.otf");

    if (!regular_path.empty()) {
        ImFontConfig cfg;
        cfg.OversampleH = 2;
        cfg.OversampleV = 1;
        g_font_regular = io.Fonts->AddFontFromFileTTF(
            regular_path.c_str(), kBaseFontSize, &cfg);
    }

    if (!semibold_path.empty()) {
        g_font_semibold = io.Fonts->AddFontFromFileTTF(
            semibold_path.c_str(), kBaseFontSize);
    }

    if (!g_font_regular) {
        g_font_regular = io.Fonts->AddFontDefaultVector();
    }
    if (!g_font_semibold) {
        g_font_semibold = g_font_regular;
    }
}

void ensure_default_dock_layout(
    ShellState* state,
    ImGuiID dockspace_id,
    const ImGuiViewport* viewport) {
    if (state == nullptr || viewport == nullptr) {
        return;
    }

    if (state->default_dock_layout_initialized &&
        state->dock_layout.dockspace_id == dockspace_id &&
        ImGui::DockBuilderGetNode(dockspace_id) != nullptr) {
        return;
    }

    state->dock_layout = {};
    state->dock_layout.dockspace_id = dockspace_id;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspace_id, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

    ImGuiID dock_center_id = dockspace_id;
    ImGuiID dock_left_id = 0;
    ImGuiID dock_bottom_id = 0;
    ImGuiID dock_left_bottom_id = 0;
    ImGui::DockBuilderSplitNode(
        dock_center_id,
        ImGuiDir_Left,
        0.28f,
        &dock_left_id,
        &dock_center_id);
    ImGui::DockBuilderSplitNode(
        dock_center_id,
        ImGuiDir_Down,
        0.30f,
        &dock_bottom_id,
        &dock_center_id);
    ImGui::DockBuilderSplitNode(
        dock_left_id,
        ImGuiDir_Down,
        0.48f,
        &dock_left_bottom_id,
        &dock_left_id);

    state->dock_layout.viewport_node_id = dock_center_id;
    state->dock_layout.timeline_node_id = dock_bottom_id;
    state->dock_layout.hierarchy_node_id = dock_left_id;
    state->dock_layout.properties_node_id = dock_left_bottom_id;

    ImGui::DockBuilderDockWindow(kViewportWindowTitle, state->dock_layout.viewport_node_id);
    ImGui::DockBuilderDockWindow(kTimelineWindowTitle, state->dock_layout.timeline_node_id);
    ImGui::DockBuilderDockWindow(kProjectWindowTitle, state->dock_layout.hierarchy_node_id);
    ImGui::DockBuilderDockWindow(kHierarchyWindowTitle, state->dock_layout.hierarchy_node_id);
    ImGui::DockBuilderDockWindow(
        kRuntimeAssetsWindowTitle,
        state->dock_layout.properties_node_id);
    ImGui::DockBuilderDockWindow(
        kConstraintsWindowTitle,
        state->dock_layout.properties_node_id);
    ImGui::DockBuilderDockWindow(kPropertiesWindowTitle, state->dock_layout.properties_node_id);
    ImGui::DockBuilderFinish(dockspace_id);

    state->default_dock_layout_initialized = true;
}

float clamp_zoom(float zoom) {
    return std::max(0.2f, std::min(zoom, 6.0f));
}

void auto_frame_skeleton(ShellState* state, ImVec2 canvas_size) {
    if (!state->preview_skeleton) {
        return;
    }
    const auto& transforms = state->preview_skeleton->bone_world_transforms();
    if (transforms.empty()) {
        return;
    }
    float min_x = std::numeric_limits<float>::max();
    float min_y = std::numeric_limits<float>::max();
    float max_x = std::numeric_limits<float>::lowest();
    float max_y = std::numeric_limits<float>::lowest();
    for (const auto& t : transforms) {
        min_x = std::min(min_x, t.world_x);
        max_x = std::max(max_x, t.world_x);
        min_y = std::min(min_y, t.world_y);
        max_y = std::max(max_y, t.world_y);
    }
    const float margin = 1.2f;
    const float bounds_w = (max_x - min_x) * margin;
    const float bounds_h = (max_y - min_y) * margin;
    const float center_x = (min_x + max_x) * 0.5f;
    const float center_y = (min_y + max_y) * 0.5f;
    if (canvas_size.x < 1.0f || canvas_size.y < 1.0f) {
        return;
    }
    const float zoom_x = canvas_size.x / std::max(bounds_w, 1.0f);
    const float zoom_y = canvas_size.y / std::max(bounds_h, 1.0f);
    state->viewport.zoom = static_cast<double>(
        clamp_zoom(std::min(zoom_x, zoom_y)));
    state->viewport.pan_x =
        static_cast<double>(canvas_size.x * 0.5f - center_x * state->viewport.zoom);
    state->viewport.pan_y =
        static_cast<double>(canvas_size.y * 0.5f + center_y * state->viewport.zoom);
    state->status_message = "Framed skeleton to viewport";
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

EditAction::EditAction(
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge)
    : kind_(kind),
      label_(std::move(label)),
      group_(std::move(group)),
      allow_merge_(allow_merge) {}

EditActionKind EditAction::kind() const {
    return kind_;
}

const std::string& EditAction::label() const {
    return label_;
}

const std::string& EditAction::group() const {
    return group_;
}

bool EditAction::allow_merge() const {
    return allow_merge_;
}

SnapshotEditAction::SnapshotEditAction(
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    EditorHistorySnapshot before,
    EditorHistorySnapshot after)
    : EditAction(kind, std::move(label), std::move(group), allow_merge),
      before_(std::move(before)),
      after_(std::move(after)) {}

bool SnapshotEditAction::undo(ShellState* state) const {
    return apply_history_snapshot(state, before_);
}

bool SnapshotEditAction::redo(ShellState* state) const {
    return apply_history_snapshot(state, after_);
}

bool SnapshotEditAction::merge_from(const EditAction& action) {
    if (!allow_merge_ || group_.empty() || !action.allow_merge() ||
        action.kind() != kind_ || action.group() != group_) {
        return false;
    }

    const auto* snapshot_action = dynamic_cast<const SnapshotEditAction*>(&action);
    if (snapshot_action == nullptr) {
        return false;
    }

    after_ = snapshot_action->after_;
    label_ = snapshot_action->label();
    return true;
}

bool UndoStack::can_undo() const {
    return !undo_actions_.empty();
}

bool UndoStack::can_redo() const {
    return !redo_actions_.empty();
}

std::size_t UndoStack::undo_count() const {
    return undo_actions_.size();
}

std::size_t UndoStack::redo_count() const {
    return redo_actions_.size();
}

void UndoStack::clear() {
    undo_actions_.clear();
    redo_actions_.clear();
}

const EditAction* UndoStack::peek_undo() const {
    return undo_actions_.empty() ? nullptr : undo_actions_.back().get();
}

const EditAction* UndoStack::peek_redo() const {
    return redo_actions_.empty() ? nullptr : redo_actions_.back().get();
}

void UndoStack::push(std::unique_ptr<EditAction> action) {
    if (!action) {
        return;
    }

    redo_actions_.clear();
    if (!undo_actions_.empty() && undo_actions_.back()->merge_from(*action)) {
        return;
    }

    if (undo_actions_.size() >= kMaxDepth) {
        undo_actions_.erase(undo_actions_.begin());
    }
    undo_actions_.push_back(std::move(action));
}

bool UndoStack::undo(ShellState* state, std::string* label_out) {
    if (undo_actions_.empty()) {
        return false;
    }

    std::unique_ptr<EditAction> action = std::move(undo_actions_.back());
    undo_actions_.pop_back();
    if (!action->undo(state)) {
        undo_actions_.push_back(std::move(action));
        return false;
    }

    if (label_out != nullptr) {
        *label_out = action->label();
    }
    redo_actions_.push_back(std::move(action));
    return true;
}

bool UndoStack::redo(ShellState* state, std::string* label_out) {
    if (redo_actions_.empty()) {
        return false;
    }

    std::unique_ptr<EditAction> action = std::move(redo_actions_.back());
    redo_actions_.pop_back();
    if (!action->redo(state)) {
        redo_actions_.push_back(std::move(action));
        return false;
    }

    if (label_out != nullptr) {
        *label_out = action->label();
    }
    undo_actions_.push_back(std::move(action));
    return true;
}

bool attachment_selection_equal(
    const std::optional<AttachmentSelection>& left,
    const std::optional<AttachmentSelection>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left.has_value()) {
        return true;
    }

    return left->slot_index == right->slot_index &&
        left->skin_index == right->skin_index &&
        left->attachment_name == right->attachment_name;
}

EditorHistorySnapshot capture_history_snapshot(
    const ShellState& state,
    bool include_serialized_project) {
    EditorHistorySnapshot snapshot;
    if (state.load_result.project != nullptr) {
        snapshot.project = *state.load_result.project;
        if (include_serialized_project) {
            snapshot.serialized_project =
                marrow::editor::serialize_project(*state.load_result.project);
        }
    }
    snapshot.preview_skin_names = state.preview_skin_names;
    snapshot.preview_slot_overrides = state.preview_slot_overrides;
    return snapshot;
}

bool history_snapshots_equal(
    const EditorHistorySnapshot& left,
    const EditorHistorySnapshot& right) {
    if (left.serialized_project != right.serialized_project ||
        left.preview_skin_names != right.preview_skin_names ||
        left.preview_slot_overrides.size() != right.preview_slot_overrides.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.preview_slot_overrides.size(); ++index) {
        if (!attachment_selection_equal(
                left.preview_slot_overrides[index],
                right.preview_slot_overrides[index])) {
            return false;
        }
    }

    return true;
}

void assign_history_snapshot(
    ShellState* state,
    const EditorHistorySnapshot& snapshot) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return;
    }

    *state->load_result.project = snapshot.project;
    state->viewport.onion_skin = snapshot.project.editor_metadata.viewport.onion_skin;
    state->preview_skin_names = snapshot.preview_skin_names;
    state->preview_slot_overrides = snapshot.preview_slot_overrides;
}

void restore_history_snapshot(
    ShellState* state,
    const EditorHistorySnapshot& snapshot) {
    if (state == nullptr) {
        return;
    }

    assign_history_snapshot(state, snapshot);
    rebuild_project_runtime(state);
}

bool apply_history_snapshot(ShellState* state, const EditorHistorySnapshot& snapshot) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const EditorHistorySnapshot current = capture_history_snapshot(*state, false);
    assign_history_snapshot(state, snapshot);
    if (!rebuild_project_runtime(state)) {
        const std::string rebuild_error = state->error_message;
        restore_history_snapshot(state, current);
        state->error_message = rebuild_error;
        return false;
    }

    return true;
}

std::unique_ptr<EditAction> make_edit_action(
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    const EditorHistorySnapshot& before,
    const EditorHistorySnapshot& after) {
    if (history_snapshots_equal(before, after)) {
        return nullptr;
    }

    switch (kind) {
    case EditActionKind::MoveBone:
        return std::make_unique<MoveBoneAction>(
            kind,
            std::move(label),
            std::move(group),
            allow_merge,
            before,
            after);
    case EditActionKind::AddKeyframe:
        return std::make_unique<AddKeyframeAction>(
            kind,
            std::move(label),
            std::move(group),
            allow_merge,
            before,
            after);
    case EditActionKind::RemoveKeyframe:
        return std::make_unique<RemoveKeyframeAction>(
            kind,
            std::move(label),
            std::move(group),
            allow_merge,
            before,
            after);
    case EditActionKind::EditProperty:
        return std::make_unique<EditPropertyAction>(
            kind,
            std::move(label),
            std::move(group),
            allow_merge,
            before,
            after);
    }

    return nullptr;
}

bool record_action_from_snapshots(
    ShellState* state,
    const EditorHistorySnapshot& before,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const EditorHistorySnapshot after = capture_history_snapshot(*state);
    state->command_stack.push(
        make_edit_action(
            kind,
            label,
            std::move(group),
            allow_merge,
            before,
            after));
    update_project_dirty_state(state);
    state->error_message.clear();
    state->status_message = std::move(label);
    return true;
}

template <typename MutateFn>
bool execute_viewport_setting_edit_action(
    ShellState* state,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const EditorHistorySnapshot before = capture_history_snapshot(*state);
    mutate();
    return record_action_from_snapshots(
        state,
        before,
        EditActionKind::EditProperty,
        std::move(label),
        std::move(group),
        allow_merge);
}

template <typename MutateFn>
bool execute_preview_edit_action(
    ShellState* state,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    std::string failure_status,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const EditorHistorySnapshot before = capture_history_snapshot(*state);
    mutate();
    if (!refresh_preview_pose(state)) {
        const std::string rebuild_error = state->error_message;
        restore_history_snapshot(state, before);
        state->error_message = rebuild_error;
        state->status_message = std::move(failure_status);
        return false;
    }

    if (state->selected_slot_index.has_value()) {
        sync_attachment_selection_for_slot(state, *state->selected_slot_index);
    }
    return record_action_from_snapshots(
        state,
        before,
        kind,
        std::move(label),
        std::move(group),
        allow_merge);
}

template <typename MutateFn>
bool apply_coalesced_project_drag(
    ShellState* state,
    bool changed,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    std::string failure_status,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const ImGuiID item_id = ImGui::GetItemID();
    if (ImGui::IsItemActivated()) {
        state->pending_edit_action = PendingEditAction{
            item_id,
            kind,
            std::move(label),
            std::move(group),
            allow_merge,
            capture_history_snapshot(*state)};
    }

    if (changed) {
        const EditorHistorySnapshot rollback = capture_history_snapshot(*state, false);
        mutate();
        if (!rebuild_project_runtime(state)) {
            const std::string rebuild_error = state->error_message;
            restore_history_snapshot(state, rollback);
            state->pending_edit_action.reset();
            state->error_message = rebuild_error;
            state->status_message = std::move(failure_status);
            return false;
        }
    }

    if (ImGui::IsItemDeactivatedAfterEdit() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        PendingEditAction pending = std::move(*state->pending_edit_action);
        state->pending_edit_action.reset();
        return record_action_from_snapshots(
            state,
            pending.before_snapshot,
            pending.kind,
            std::move(pending.label),
            std::move(pending.group),
            pending.allow_merge);
    }

    if (ImGui::IsItemDeactivated() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        state->pending_edit_action.reset();
    }

    return true;
}

template <typename MutateFn>
bool apply_onion_skin_edit(
    ShellState* state,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    return execute_viewport_setting_edit_action(
        state,
        std::move(label),
        std::move(group),
        allow_merge,
        [&]() {
            auto settings = state->viewport.onion_skin;
            mutate(&settings);
            state->viewport.onion_skin = settings;
            state->load_result.project->editor_metadata.viewport.onion_skin = settings;
        });
}

template <typename MutateFn>
bool apply_debug_overlay_edit(
    ShellState* state,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    return execute_viewport_setting_edit_action(
        state,
        std::move(label),
        std::move(group),
        allow_merge,
        [&]() {
            auto settings = state->viewport.debug_overlay;
            mutate(&settings);
            state->viewport.debug_overlay = settings;
            state->load_result.project->editor_metadata.viewport.debug_overlay = settings;
        });
}

template <typename MutateFn>
bool apply_coalesced_viewport_drag(
    ShellState* state,
    bool changed,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const ImGuiID item_id = ImGui::GetItemID();
    if (ImGui::IsItemActivated()) {
        state->pending_edit_action = PendingEditAction{
            item_id,
            EditActionKind::EditProperty,
            std::move(label),
            std::move(group),
            allow_merge,
            capture_history_snapshot(*state)};
    }

    if (changed) {
        mutate();
        update_project_dirty_state(state);
    }

    if (ImGui::IsItemDeactivatedAfterEdit() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        PendingEditAction pending = std::move(*state->pending_edit_action);
        state->pending_edit_action.reset();
        return record_action_from_snapshots(
            state,
            pending.before_snapshot,
            pending.kind,
            std::move(pending.label),
            std::move(pending.group),
            pending.allow_merge);
    }

    if (ImGui::IsItemDeactivated() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        state->pending_edit_action.reset();
    }

    return true;
}

template <typename MutateFn>
bool apply_coalesced_onion_skin_drag(
    ShellState* state,
    bool changed,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    return apply_coalesced_viewport_drag(
        state,
        changed,
        std::move(label),
        std::move(group),
        allow_merge,
        [&]() {
            auto settings = state->viewport.onion_skin;
            mutate(&settings);
            state->viewport.onion_skin = settings;
            state->load_result.project->editor_metadata.viewport.onion_skin = settings;
        });
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
    edit.softness = constraint->softness;
    edit.compress = constraint->compress;
    edit.stretch = constraint->stretch;
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
    edit.step = constraint->step;
    edit.x = constraint->x;
    edit.y = constraint->y;
    edit.rotate = constraint->rotate;
    edit.scale_x = constraint->scale_x;
    edit.shear_x = constraint->shear_x;
    edit.limit = constraint->limit;
    edit.inertia = constraint->inertia;
    edit.damping = constraint->damping;
    edit.strength = constraint->strength;
    edit.mass_inverse = constraint->mass_inverse;
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
    edit.step = 1.0 / 60.0;
    edit.x = 1.0;
    edit.y = 1.0;
    edit.rotate = 1.0;
    edit.scale_x = 1.0;
    edit.shear_x = 0.0;
    edit.limit = 30.0;
    edit.inertia = 0.85;
    edit.damping = 4.0;
    edit.strength = 18.0;
    edit.mass_inverse = 1.0;
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
    bool update_status_message,
    bool record_history) {
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

    if (!record_history) {
        if (enabled) {
            if (existing == state->preview_skin_names.end()) {
                state->preview_skin_names.push_back(skin_name);
            }
        } else if (existing != state->preview_skin_names.end()) {
            state->preview_skin_names.erase(existing);
        }
        return apply_preview_skin_selection(state, "Skin Preview", update_status_message);
    }

    return execute_preview_edit_action(
        state,
        EditActionKind::EditProperty,
        std::string(enabled ? "Enabled preview skin " : "Disabled preview skin ") + skin_name,
        "preview-skins",
        true,
        "Preview skin change failed",
        [&]() {
            if (enabled) {
                if (existing == state->preview_skin_names.end()) {
                    state->preview_skin_names.push_back(skin_name);
                }
            } else if (existing != state->preview_skin_names.end()) {
                state->preview_skin_names.erase(existing);
            }
        });
}

bool apply_attachment_selection_to_preview_slot(
    ShellState* state,
    const AttachmentSelection& selection,
    std::string_view source,
    bool update_status_message,
    bool record_history) {
    if (!state->load_result || !state->preview_skeleton ||
        selection.slot_index >= state->preview_skeleton->slot_states().size()) {
        return false;
    }

    if (!resolve_attachment_reference(*state->load_result.skeleton_data, selection).has_value()) {
        return false;
    }

    const auto apply_selection = [&]() {
        if (selection.slot_index >= state->preview_slot_overrides.size()) {
            state->preview_slot_overrides.resize(state->preview_skeleton->slot_states().size());
        }
        state->preview_slot_overrides[selection.slot_index] = selection;
        select_attachment(state, selection, source, false);
    };

    if (!record_history) {
        apply_selection();
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

    const std::string slot_name =
        state->load_result.skeleton_data->slots()[selection.slot_index].name;
    return execute_preview_edit_action(
        state,
        EditActionKind::EditProperty,
        "Swapped preview attachment on " + slot_name + " to " + selection.attachment_name,
        "preview-slot:" + slot_name,
        true,
        "Preview attachment swap failed",
        apply_selection);
}

bool reset_preview_slot_to_skin_selection(
    ShellState* state,
    std::size_t slot_index,
    std::string_view source,
    bool update_status_message,
    bool record_history) {
    if (!state->load_result || !state->preview_skeleton ||
        slot_index >= state->preview_skeleton->slot_states().size()) {
        return false;
    }

    const auto reset_selection = [&]() {
        if (slot_index >= state->preview_slot_overrides.size()) {
            state->preview_slot_overrides.resize(state->preview_skeleton->slot_states().size());
        }
        state->preview_slot_overrides[slot_index].reset();
        if (const auto preview_selection = current_attachment_selection(*state, slot_index)) {
            select_attachment(state, preview_selection, source, false);
        } else {
            state->selected_attachment.reset();
        }
    };

    if (!record_history) {
        reset_selection();
        if (!refresh_preview_pose(state)) {
            return false;
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

    const std::string slot_name = state->load_result.skeleton_data->slots()[slot_index].name;
    return execute_preview_edit_action(
        state,
        EditActionKind::EditProperty,
        "Reset preview slot " + slot_name + " to the active skin composition",
        "preview-slot:" + slot_name,
        true,
        "Preview attachment reset failed",
        reset_selection);
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
            static_cast<double>(attachment.point_attachment->local_position.x),
            static_cast<double>(attachment.point_attachment->local_position.y));
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

const char* onion_skin_mode_name(marrow::editor::OnionSkinMode mode) {
    switch (mode) {
    case marrow::editor::OnionSkinMode::Frame:
        return "Frame";
    case marrow::editor::OnionSkinMode::Keyframe:
        return "Keyframe";
    }

    return "Frame";
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

const char* weight_paint_mode_name(WeightPaintMode mode) {
    switch (mode) {
    case WeightPaintMode::Paint:
        return "Paint";
    case WeightPaintMode::Erase:
        return "Erase";
    case WeightPaintMode::Smooth:
        return "Smooth";
    }

    return "Paint";
}

ImVec4 interpolate_color(const ImVec4& start, const ImVec4& end, float alpha) {
    return ImVec4(
        start.x + ((end.x - start.x) * alpha),
        start.y + ((end.y - start.y) * alpha),
        start.z + ((end.z - start.z) * alpha),
        start.w + ((end.w - start.w) * alpha));
}

ImVec4 mesh_weight_heatmap_color(double weight, float alpha) {
    const float t = std::clamp(static_cast<float>(weight), 0.0f, 1.0f);
    const ImVec4 blue(0.12f, 0.33f, 0.95f, alpha);
    const ImVec4 green(0.13f, 0.74f, 0.39f, alpha);
    const ImVec4 yellow(0.96f, 0.83f, 0.24f, alpha);
    const ImVec4 red(0.91f, 0.28f, 0.17f, alpha);

    if (t <= (1.0f / 3.0f)) {
        return interpolate_color(blue, green, t * 3.0f);
    }
    if (t <= (2.0f / 3.0f)) {
        return interpolate_color(green, yellow, (t - (1.0f / 3.0f)) * 3.0f);
    }
    return interpolate_color(yellow, red, (t - (2.0f / 3.0f)) * 3.0f);
}

std::optional<marrow::runtime::AttachmentVertex> inverse_transform_point_safe(
    const marrow::runtime::BoneWorldTransform& transform,
    double world_x,
    double world_y) {
    constexpr double kEpsilon = 1e-8;
    const double determinant =
        (static_cast<double>(transform.a) * static_cast<double>(transform.d)) -
        (static_cast<double>(transform.b) * static_cast<double>(transform.c));
    if (std::abs(determinant) <= kEpsilon) {
        return std::nullopt;
    }

    const double inverse_determinant = 1.0 / determinant;
    const double translated_x = world_x - static_cast<double>(transform.world_x);
    const double translated_y = world_y - static_cast<double>(transform.world_y);
    return marrow::runtime::AttachmentVertex{
        ((translated_x * static_cast<double>(transform.d)) -
         (translated_y * static_cast<double>(transform.b))) *
            inverse_determinant,
        ((translated_y * static_cast<double>(transform.a)) -
         (translated_x * static_cast<double>(transform.c))) *
            inverse_determinant};
}

double weight_for_bone(
    const marrow::runtime::MeshGeometry::VertexWeights& vertex_weights,
    std::size_t bone_index) {
    for (const auto& influence : vertex_weights.influences) {
        if (influence.bone_index == bone_index) {
            return influence.weight;
        }
    }
    return 0.0;
}

std::vector<std::vector<std::size_t>> build_mesh_vertex_neighbors(
    const std::vector<std::size_t>& triangles,
    std::size_t vertex_count) {
    std::vector<std::vector<std::size_t>> neighbors(vertex_count);
    for (std::size_t index = 0; index + 2U < triangles.size(); index += 3U) {
        const std::size_t a = triangles[index];
        const std::size_t b = triangles[index + 1U];
        const std::size_t c = triangles[index + 2U];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count) {
            continue;
        }

        const auto connect = [&](std::size_t from, std::size_t to) {
            auto& entries = neighbors[from];
            if (std::find(entries.begin(), entries.end(), to) == entries.end()) {
                entries.push_back(to);
            }
        };
        connect(a, b);
        connect(a, c);
        connect(b, a);
        connect(b, c);
        connect(c, a);
        connect(c, b);
    }

    return neighbors;
}

marrow::editor::MeshWeightAttachmentEdit build_mesh_weight_attachment_edit_from_runtime(
    const MeshWeightPaintTarget& target,
    const marrow::runtime::SkeletonData& skeleton) {
    marrow::editor::MeshWeightAttachmentEdit edit;
    edit.skin_name = target.source_skin_name;
    edit.slot_name = target.slot_name;
    edit.attachment_name = target.source_attachment_name;
    if (target.source_attachment == nullptr || target.source_attachment->mesh_geometry == nullptr) {
        return edit;
    }

    edit.vertices.reserve(target.source_attachment->mesh_geometry->weights.size());
    for (const auto& source_vertex : target.source_attachment->mesh_geometry->weights) {
        marrow::editor::MeshWeightVertexEdit vertex;
        vertex.influences.reserve(source_vertex.influences.size());
        for (const auto& source_influence : source_vertex.influences) {
            const std::string bone_name =
                source_influence.bone_index < skeleton.bones().size()
                ? skeleton.bones()[source_influence.bone_index].name
                : ("<bone " + std::to_string(source_influence.bone_index) + ">");
            vertex.influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
                bone_name,
                source_influence.x,
                source_influence.y,
                source_influence.weight});
        }
        edit.vertices.push_back(std::move(vertex));
    }

    return edit;
}

bool mesh_weight_vertex_equal(
    const marrow::editor::MeshWeightVertexEdit& left,
    const marrow::editor::MeshWeightVertexEdit& right,
    double tolerance = 1e-6) {
    if (left.influences.size() != right.influences.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.influences.size(); ++index) {
        const auto& lhs = left.influences[index];
        const auto& rhs = right.influences[index];
        if (lhs.bone_name != rhs.bone_name ||
            std::abs(lhs.x - rhs.x) > tolerance ||
            std::abs(lhs.y - rhs.y) > tolerance ||
            std::abs(lhs.weight - rhs.weight) > tolerance) {
            return false;
        }
    }

    return true;
}

void normalize_mesh_weight_vertex_edit(marrow::editor::MeshWeightVertexEdit* vertex) {
    if (vertex == nullptr) {
        return;
    }

    constexpr double kWeightEpsilon = 1e-6;
    auto& influences = vertex->influences;
    influences.erase(
        std::remove_if(
            influences.begin(),
            influences.end(),
            [](const marrow::editor::MeshWeightInfluenceEdit& influence) {
                return influence.weight <= kWeightEpsilon;
            }),
        influences.end());
    if (influences.empty()) {
        return;
    }

    if (influences.size() > 4U) {
        std::stable_sort(
            influences.begin(),
            influences.end(),
            [](const marrow::editor::MeshWeightInfluenceEdit& lhs,
               const marrow::editor::MeshWeightInfluenceEdit& rhs) {
                return lhs.weight > rhs.weight;
            });
        influences.resize(4U);
    }

    double total_weight = 0.0;
    for (const auto& influence : influences) {
        total_weight += influence.weight;
    }
    if (total_weight <= kWeightEpsilon) {
        influences.clear();
        return;
    }

    for (auto& influence : influences) {
        influence.weight /= total_weight;
    }
}

void store_mesh_weight_attachment_edit(
    marrow::editor::ProjectData* project,
    marrow::editor::MeshWeightAttachmentEdit edit) {
    if (project == nullptr) {
        return;
    }

    marrow::editor::MeshWeightAttachmentEdit* existing =
        project->find_mesh_weight_attachment_edit(
            edit.skin_name,
            edit.slot_name,
            edit.attachment_name);
    if (existing != nullptr) {
        *existing = std::move(edit);
    } else {
        project->mesh_weight_attachment_edits.push_back(std::move(edit));
    }
}

std::optional<MeshWeightPaintTarget> current_mesh_weight_paint_target(const ShellState& state) {
    if (!state.load_result || !state.preview_skeleton || !state.selected_slot_index.has_value()) {
        return std::nullopt;
    }

    const std::size_t slot_index = *state.selected_slot_index;
    const auto& skeleton = *state.load_result.skeleton_data;
    if (slot_index >= skeleton.slots().size()) {
        return std::nullopt;
    }

    const marrow::runtime::AttachmentData* display_attachment =
        state.preview_skeleton->current_attachment(slot_index);
    if (display_attachment == nullptr || display_attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }

    MeshWeightPaintTarget target;
    target.slot_index = slot_index;
    target.slot_name = skeleton.slots()[slot_index].name;
    target.display_attachment_name = display_attachment->name;
    target.display_attachment = display_attachment;

    if (display_attachment->kind == marrow::runtime::AttachmentKind::LinkedMesh &&
        display_attachment->linked_mesh.has_value()) {
        target.source_skin_index =
            display_attachment->linked_mesh->parent_skin_index.has_value()
                ? display_attachment->linked_mesh->parent_skin_index
                : skeleton.default_skin_index();
        target.source_attachment_name =
            display_attachment->linked_mesh->parent_attachment;
        target.source_attachment =
            target.source_skin_index.has_value()
                ? skeleton.find_attachment(
                      *target.source_skin_index,
                      slot_index,
                      target.source_attachment_name)
                : nullptr;
    } else {
        std::optional<std::size_t> source_skin_index;
        target.source_attachment =
            skeleton.find_attachment_source(slot_index, display_attachment->name, &source_skin_index);
        target.source_skin_index = source_skin_index;
        target.source_attachment_name = display_attachment->name;
    }

    if (target.source_attachment == nullptr || target.source_attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }

    target.source_skin_name = source_skin_name(skeleton, target.source_skin_index);
    return target;
}

std::optional<MeshWeightOverlay> build_mesh_weight_overlay(
    const ShellState& state,
    const ViewportLayout& layout) {
    const std::optional<MeshWeightPaintTarget> target = current_mesh_weight_paint_target(state);
    if (!target.has_value() || !state.preview_skeleton) {
        return std::nullopt;
    }

    const std::optional<marrow::runtime::MeshAttachmentPose> pose =
        state.preview_skeleton->evaluate_current_mesh_attachment(target->slot_index);
    if (!pose.has_value() ||
        target->display_attachment == nullptr ||
        target->display_attachment->mesh_geometry == nullptr) {
        return std::nullopt;
    }

    const auto& geometry = *target->display_attachment->mesh_geometry;
    if (pose->vertices.size() != geometry.weights.size()) {
        return std::nullopt;
    }

    MeshWeightOverlay overlay;
    overlay.target = *target;
    overlay.triangles = geometry.triangles;
    overlay.neighbors = build_mesh_vertex_neighbors(geometry.triangles, pose->vertices.size());
    const std::vector<double>* vertex_offsets =
        state.preview_skeleton->current_mesh_vertex_offsets(target->slot_index);
    if (vertex_offsets != nullptr) {
        overlay.vertex_offsets = *vertex_offsets;
    } else {
        overlay.vertex_offsets.assign(pose->vertices.size() * 2U, 0.0);
    }

    overlay.vertices.reserve(pose->vertices.size());
    for (std::size_t vertex_index = 0; vertex_index < pose->vertices.size(); ++vertex_index) {
        const double selected_weight =
            state.selected_bone_index.has_value()
                ? weight_for_bone(geometry.weights[vertex_index], *state.selected_bone_index)
                : 0.0;
        overlay.vertices.push_back(MeshWeightOverlayVertex{
            screen_from_world(
                layout,
                pose->vertices[vertex_index].x,
                pose->vertices[vertex_index].y),
            pose->vertices[vertex_index],
            selected_weight});
    }

    return overlay;
}

bool apply_paint_weight_to_vertex(
    const ShellState& state,
    const MeshWeightOverlay& overlay,
    std::size_t vertex_index,
    double stamp_strength,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    if (!state.load_result || !state.selected_bone_index.has_value() ||
        vertex == nullptr ||
        vertex_index >= overlay.vertices.size() ||
        *state.selected_bone_index >= state.load_result.skeleton_data->bones().size() ||
        *state.selected_bone_index >= state.preview_skeleton->bone_world_transforms().size()) {
        return false;
    }

    marrow::editor::MeshWeightVertexEdit updated = *vertex;
    const auto& skeleton = *state.load_result.skeleton_data;
    const std::string active_bone_name = skeleton.bones()[*state.selected_bone_index].name;
    auto influence_it = std::find_if(
        updated.influences.begin(),
        updated.influences.end(),
        [&](const marrow::editor::MeshWeightInfluenceEdit& influence) {
            return influence.bone_name == active_bone_name;
        });

    if (influence_it == updated.influences.end()) {
        const double offset_x =
            (vertex_index * 2U) < overlay.vertex_offsets.size()
                ? overlay.vertex_offsets[vertex_index * 2U]
                : 0.0;
        const double offset_y =
            ((vertex_index * 2U) + 1U) < overlay.vertex_offsets.size()
                ? overlay.vertex_offsets[(vertex_index * 2U) + 1U]
                : 0.0;
        const auto bind_position = inverse_transform_point_safe(
            state.preview_skeleton->bone_world_transforms()[*state.selected_bone_index],
            overlay.vertices[vertex_index].world_position.x,
            overlay.vertices[vertex_index].world_position.y);
        if (!bind_position.has_value()) {
            return false;
        }

        updated.influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
            active_bone_name,
            static_cast<double>(bind_position->x) - offset_x,
            static_cast<double>(bind_position->y) - offset_y,
            0.0});
        influence_it = updated.influences.end() - 1;
    }

    influence_it->weight += stamp_strength;
    normalize_mesh_weight_vertex_edit(&updated);
    if (updated.influences.empty() || mesh_weight_vertex_equal(*vertex, updated)) {
        return false;
    }

    *vertex = std::move(updated);
    return true;
}

bool apply_erase_weight_to_vertex(
    const ShellState& state,
    double stamp_strength,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    if (!state.load_result || !state.selected_bone_index.has_value() || vertex == nullptr) {
        return false;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    if (*state.selected_bone_index >= skeleton.bones().size()) {
        return false;
    }

    if (vertex->influences.size() <= 1U) {
        return false;
    }

    marrow::editor::MeshWeightVertexEdit updated = *vertex;
    const std::string active_bone_name = skeleton.bones()[*state.selected_bone_index].name;
    auto influence_it = std::find_if(
        updated.influences.begin(),
        updated.influences.end(),
        [&](const marrow::editor::MeshWeightInfluenceEdit& influence) {
            return influence.bone_name == active_bone_name;
        });
    if (influence_it == updated.influences.end()) {
        return false;
    }

    influence_it->weight = std::max(0.0, influence_it->weight - stamp_strength);
    normalize_mesh_weight_vertex_edit(&updated);
    if (updated.influences.empty() || mesh_weight_vertex_equal(*vertex, updated)) {
        return false;
    }

    *vertex = std::move(updated);
    return true;
}

bool apply_smooth_weight_to_vertex(
    const std::vector<marrow::editor::MeshWeightVertexEdit>& source_vertices,
    const MeshWeightOverlay& overlay,
    std::size_t vertex_index,
    double stamp_strength,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    if (vertex == nullptr || vertex_index >= overlay.neighbors.size()) {
        return false;
    }

    struct AveragedInfluence {
        std::string bone_name;
        double average_weight{0.0};
        double bind_x_sum{0.0};
        double bind_y_sum{0.0};
        double bind_weight_sum{0.0};
    };

    const auto accumulate_vertex = [&](const marrow::editor::MeshWeightVertexEdit& sample_vertex,
                                       std::vector<AveragedInfluence>* averages) {
        for (const auto& influence : sample_vertex.influences) {
            auto averaged_it = std::find_if(
                averages->begin(),
                averages->end(),
                [&](const AveragedInfluence& averaged) {
                    return averaged.bone_name == influence.bone_name;
                });
            if (averaged_it == averages->end()) {
                averages->push_back(AveragedInfluence{influence.bone_name});
                averaged_it = averages->end() - 1;
            }

            averaged_it->average_weight += influence.weight;
            averaged_it->bind_x_sum += influence.x * influence.weight;
            averaged_it->bind_y_sum += influence.y * influence.weight;
            averaged_it->bind_weight_sum += influence.weight;
        }
    };

    std::vector<AveragedInfluence> averages;
    averages.reserve(8U);
    int sample_count = 1;
    accumulate_vertex(*vertex, &averages);

    for (const std::size_t neighbor_index : overlay.neighbors[vertex_index]) {
        if (neighbor_index >= source_vertices.size()) {
            continue;
        }

        accumulate_vertex(source_vertices[neighbor_index], &averages);
        ++sample_count;
    }

    if (sample_count <= 1) {
        return false;
    }

    std::vector<std::string> ordered_bones;
    ordered_bones.reserve(averages.size());
    for (const auto& influence : vertex->influences) {
        ordered_bones.push_back(influence.bone_name);
    }
    for (const AveragedInfluence& averaged : averages) {
        if (std::find(ordered_bones.begin(), ordered_bones.end(), averaged.bone_name) ==
            ordered_bones.end()) {
            ordered_bones.push_back(averaged.bone_name);
        }
    }

    marrow::editor::MeshWeightVertexEdit updated;
    updated.influences.reserve(ordered_bones.size());
    constexpr double kWeightEpsilon = 1e-6;
    for (const std::string& bone_name : ordered_bones) {
        const auto current_it = std::find_if(
            vertex->influences.begin(),
            vertex->influences.end(),
            [&](const marrow::editor::MeshWeightInfluenceEdit& influence) {
                return influence.bone_name == bone_name;
            });
        const auto averaged_it = std::find_if(
            averages.begin(),
            averages.end(),
            [&](const AveragedInfluence& averaged) {
                return averaged.bone_name == bone_name;
            });

        const double current_weight =
            current_it != vertex->influences.end() ? current_it->weight : 0.0;
        const double average_weight =
            averaged_it != averages.end()
                ? averaged_it->average_weight / static_cast<double>(sample_count)
                : 0.0;
        const double blended_weight =
            current_weight + ((average_weight - current_weight) * stamp_strength);
        if (blended_weight <= kWeightEpsilon) {
            continue;
        }

        double bind_x = current_it != vertex->influences.end() ? current_it->x : 0.0;
        double bind_y = current_it != vertex->influences.end() ? current_it->y : 0.0;
        if (averaged_it != averages.end() && averaged_it->bind_weight_sum > kWeightEpsilon) {
            const double average_bind_x = averaged_it->bind_x_sum / averaged_it->bind_weight_sum;
            const double average_bind_y = averaged_it->bind_y_sum / averaged_it->bind_weight_sum;
            if (current_it != vertex->influences.end()) {
                bind_x = current_it->x + ((average_bind_x - current_it->x) * stamp_strength);
                bind_y = current_it->y + ((average_bind_y - current_it->y) * stamp_strength);
            } else {
                bind_x = average_bind_x;
                bind_y = average_bind_y;
            }
        }

        updated.influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
            bone_name,
            bind_x,
            bind_y,
            blended_weight});
    }

    normalize_mesh_weight_vertex_edit(&updated);
    if (updated.influences.empty() || mesh_weight_vertex_equal(*vertex, updated)) {
        return false;
    }

    *vertex = std::move(updated);
    return true;
}

std::string weight_paint_stroke_label(
    const ShellState& state,
    const MeshWeightPaintTarget& target) {
    std::string bone_name = "<bone>";
    if (state.load_result &&
        state.selected_bone_index.has_value() &&
        *state.selected_bone_index < state.load_result.skeleton_data->bones().size()) {
        bone_name = state.load_result.skeleton_data->bones()[*state.selected_bone_index].name;
    }

    switch (state.weight_paint.mode) {
    case WeightPaintMode::Paint:
        return "Painted " + bone_name + " weights on " + target.source_attachment_name;
    case WeightPaintMode::Erase:
        return "Erased " + bone_name + " weights on " + target.source_attachment_name;
    case WeightPaintMode::Smooth:
        return "Smoothed weights on " + target.source_attachment_name;
    }

    return "Edited mesh weights";
}

void reset_weight_paint_stroke(ShellState* state) {
    if (state == nullptr) {
        return;
    }

    state->weight_paint_stroke.active = false;
    state->weight_paint_stroke.changed = false;
    state->weight_paint_stroke.label.clear();
    state->weight_paint_stroke.group.clear();
    state->weight_paint_stroke.has_last_sample = false;
}

void begin_weight_paint_stroke(
    ShellState* state,
    const MeshWeightPaintTarget& target) {
    if (state == nullptr || state->weight_paint_stroke.active || !state->load_result) {
        return;
    }

    state->weight_paint_stroke.active = true;
    state->weight_paint_stroke.changed = false;
    state->weight_paint_stroke.before_snapshot = capture_history_snapshot(*state);
    state->weight_paint_stroke.label = weight_paint_stroke_label(*state, target);
    state->weight_paint_stroke.group =
        "mesh-weight:" + target.source_skin_name + ":" + target.slot_name + ":" +
        target.source_attachment_name;
    state->weight_paint_stroke.has_last_sample = false;
}

bool finish_weight_paint_stroke(ShellState* state) {
    if (state == nullptr || !state->weight_paint_stroke.active) {
        return false;
    }

    const bool changed = state->weight_paint_stroke.changed;
    const EditorHistorySnapshot before_snapshot = state->weight_paint_stroke.before_snapshot;
    const std::string label = state->weight_paint_stroke.label;
    const std::string group = state->weight_paint_stroke.group;
    reset_weight_paint_stroke(state);

    if (!changed) {
        return false;
    }

    return record_action_from_snapshots(
        state,
        before_snapshot,
        EditActionKind::EditProperty,
        label,
        group,
        false);
}

bool apply_weight_paint_sample(
    ShellState* state,
    const MeshWeightOverlay& overlay,
    const ImVec2& screen_position) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    if ((state->weight_paint.mode == WeightPaintMode::Paint ||
         state->weight_paint.mode == WeightPaintMode::Erase) &&
        (!state->selected_bone_index.has_value() ||
         *state->selected_bone_index >= state->load_result.skeleton_data->bones().size())) {
        return false;
    }

    marrow::editor::MeshWeightAttachmentEdit next_edit =
        build_mesh_weight_attachment_edit_from_runtime(
            overlay.target,
            *state->load_result.skeleton_data);
    if (next_edit.vertices.empty()) {
        return false;
    }

    const std::vector<marrow::editor::MeshWeightVertexEdit> smooth_source_vertices =
        next_edit.vertices;
    bool changed = false;
    for (std::size_t vertex_index = 0; vertex_index < overlay.vertices.size(); ++vertex_index) {
        if (vertex_index >= next_edit.vertices.size()) {
            break;
        }

        const float distance = std::sqrt(
            squared_distance(screen_position, overlay.vertices[vertex_index].screen_position));
        if (distance > state->weight_paint.radius_pixels) {
            continue;
        }

        const double falloff = 1.0 -
            (static_cast<double>(distance) /
             std::max(static_cast<double>(state->weight_paint.radius_pixels), 1.0));
        const double stamp_strength =
            std::clamp(
                static_cast<double>(state->weight_paint.strength) * falloff,
                0.0,
                1.0);
        if (stamp_strength <= 1e-6) {
            continue;
        }

        bool vertex_changed = false;
        switch (state->weight_paint.mode) {
        case WeightPaintMode::Paint:
            vertex_changed = apply_paint_weight_to_vertex(
                *state,
                overlay,
                vertex_index,
                stamp_strength,
                &next_edit.vertices[vertex_index]);
            break;
        case WeightPaintMode::Erase:
            vertex_changed = apply_erase_weight_to_vertex(
                *state,
                stamp_strength,
                &next_edit.vertices[vertex_index]);
            break;
        case WeightPaintMode::Smooth:
            vertex_changed = apply_smooth_weight_to_vertex(
                smooth_source_vertices,
                overlay,
                vertex_index,
                stamp_strength,
                &next_edit.vertices[vertex_index]);
            break;
        }

        changed = changed || vertex_changed;
    }

    if (!changed) {
        return false;
    }

    const marrow::editor::ProjectData previous_project = *state->load_result.project;
    store_mesh_weight_attachment_edit(state->load_result.project.get(), std::move(next_edit));
    if (!rebuild_project_runtime(state)) {
        *state->load_result.project = previous_project;
        state->status_message = "Weight paint stroke failed";
        return false;
    }

    update_project_dirty_state(state);
    state->weight_paint_stroke.changed = true;
    return true;
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

template <typename Timeline>
void append_timeline_key_times(
    const std::vector<Timeline>& timelines,
    std::vector<double>* key_times) {
    if (key_times == nullptr) {
        return;
    }

    for (const Timeline& timeline : timelines) {
        for (const auto& keyframe : timeline.keyframes) {
            key_times->push_back(static_cast<double>(keyframe.time));
        }
    }
}

std::vector<double> collect_animation_key_times(const marrow::runtime::AnimationData& animation) {
    std::vector<double> key_times;
    append_timeline_key_times(animation.bone_rotate_timelines, &key_times);
    append_timeline_key_times(animation.bone_inherit_timelines, &key_times);
    append_timeline_key_times(animation.bone_translate_timelines, &key_times);
    append_timeline_key_times(animation.bone_scale_timelines, &key_times);
    append_timeline_key_times(animation.bone_shear_timelines, &key_times);
    append_timeline_key_times(animation.slot_attachment_timelines, &key_times);
    append_timeline_key_times(animation.slot_color_timelines, &key_times);
    append_timeline_key_times(animation.mesh_deform_timelines, &key_times);
    if (animation.draw_order_timeline_data.has_value()) {
        for (const auto& keyframe : animation.draw_order_timeline_data->keyframes) {
            key_times.push_back(static_cast<double>(keyframe.time));
        }
    }
    if (animation.event_timeline_data.has_value()) {
        for (const auto& keyframe : animation.event_timeline_data->keyframes) {
            key_times.push_back(static_cast<double>(keyframe.time));
        }
    }

    std::sort(key_times.begin(), key_times.end());
    key_times.erase(
        std::unique(
            key_times.begin(),
            key_times.end(),
            [](double lhs, double rhs) { return std::abs(lhs - rhs) <= 1e-6; }),
        key_times.end());
    return key_times;
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
        key_times.push_back(static_cast<double>(keyframe.time));
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

void apply_preview_slot_overrides(
    const ShellState& state,
    marrow::runtime::Skeleton* skeleton) {
    if (!state.load_result || skeleton == nullptr) {
        return;
    }

    auto& slot_states = skeleton->slot_states();
    auto& mesh_deforms = skeleton->mesh_deform_states();
    for (std::size_t slot_index = 0;
         slot_index < state.preview_slot_overrides.size() && slot_index < slot_states.size();
         ++slot_index) {
        const auto& override_selection = state.preview_slot_overrides[slot_index];
        if (!override_selection.has_value()) {
            continue;
        }

        if (!resolve_attachment_reference(
                *state.load_result.skeleton_data,
                *override_selection).has_value()) {
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

void apply_preview_slot_overrides(ShellState* state) {
    if (!state->load_result || !state->preview_skeleton) {
        return;
    }

    for (std::size_t slot_index = 0; slot_index < state->preview_slot_overrides.size(); ++slot_index) {
        const auto& override_selection = state->preview_slot_overrides[slot_index];
        if (!override_selection.has_value()) {
            continue;
        }
        if (!resolve_attachment_reference(
                *state->load_result.skeleton_data,
                *override_selection).has_value()) {
            state->preview_slot_overrides[slot_index].reset();
        }
    }

    apply_preview_slot_overrides(*state, state->preview_skeleton.get());
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
        copied.time = static_cast<double>(keyframe.time);
        copied.angle = static_cast<double>(keyframe.angle);
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
        copied.time = static_cast<double>(keyframe.time);
        copied.x = static_cast<double>(keyframe.x);
        copied.y = static_cast<double>(keyframe.y);
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
        copied.time = static_cast<double>(keyframe.time);
        copied.vertex_offsets.reserve(keyframe.vertex_offsets.size());
        for (const marrow::runtime::AnimationScalar offset : keyframe.vertex_offsets) {
            copied.vertex_offsets.push_back(static_cast<double>(offset));
        }
        copied.interpolation = keyframe.interpolation;
        edit->keyframes.push_back(std::move(copied));
    }
}

std::optional<double> widen_animation_optional(
    const std::optional<marrow::runtime::AnimationScalar>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<double>(*value);
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
        copied.time = static_cast<double>(keyframe.time);
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
        copied.time = static_cast<double>(keyframe.time);
        copied.event_name =
            state.load_result.skeleton_data->events()[keyframe.event_index].name;
        copied.int_value = keyframe.int_value;
        copied.float_value = widen_animation_optional(keyframe.float_value);
        copied.string_value = keyframe.string_value;
        copied.audio_path = keyframe.audio_path;
        copied.volume = widen_animation_optional(keyframe.volume);
        copied.balance = widen_animation_optional(keyframe.balance);
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
        keyframe.angle = static_cast<double>(pose.rotation);
        break;
    case marrow::editor::TransformTimelineChannel::Translate:
        keyframe.x = static_cast<double>(pose.x);
        keyframe.y = static_cast<double>(pose.y);
        break;
    case marrow::editor::TransformTimelineChannel::Scale:
        keyframe.x = static_cast<double>(pose.scale_x);
        keyframe.y = static_cast<double>(pose.scale_y);
        break;
    case marrow::editor::TransformTimelineChannel::Shear:
        keyframe.x = static_cast<double>(pose.shear_x);
        keyframe.y = static_cast<double>(pose.shear_y);
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

    std::optional<marrow::runtime::AnimationStateSnapshot> playback_snapshot;
    if (state->animation_state != nullptr) {
        playback_snapshot = state->animation_state->capture_state();
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
        marrow::allocate_unique<marrow::runtime::Skeleton>(state->load_result.skeleton_data);
    state->animation_state =
        marrow::allocate_unique<marrow::runtime::AnimationState>(
            state->load_result.skeleton_data);
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

    bool restored_playback = false;
    if (playback_snapshot.has_value()) {
        for (const auto& track_root : playback_snapshot->track_roots) {
            if (!track_root.has_value()) {
                continue;
            }
            restored_playback = restore_preview_playback(state, *playback_snapshot);
            break;
        }
    }

    if (!restored_playback && !refresh_preview_pose(state)) {
        return false;
    }
    if (state->selected_slot_index.has_value()) {
        sync_attachment_selection_for_slot(state, *state->selected_slot_index);
    }

    state->error_message.clear();
    return true;
}

void update_project_dirty_state(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        state->project_dirty = false;
        return;
    }

    state->project_dirty =
        marrow::editor::serialize_project(*state->load_result.project) !=
        state->saved_project_snapshot;
}

bool apply_project_command_change(
    ShellState* state,
    const marrow::editor::ProjectData& previous_project,
    EditActionKind kind,
    std::string command_label,
    std::string group,
    bool allow_merge,
    std::string failure_status) {
    EditorHistorySnapshot before = capture_history_snapshot(*state);
    before.project = previous_project;
    before.serialized_project = marrow::editor::serialize_project(previous_project);

    if (!rebuild_project_runtime(state)) {
        const std::string rebuild_error = state->error_message;
        restore_history_snapshot(state, before);
        state->error_message = rebuild_error;
        state->status_message = std::move(failure_status);
        return false;
    }

    return record_action_from_snapshots(
        state,
        before,
        kind,
        std::move(command_label),
        std::move(group),
        allow_merge);
}

bool undo_project_change(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    std::string label;
    if (!state->command_stack.undo(state, &label)) {
        state->status_message =
            state->command_stack.can_undo() ? "Undo failed" : "Nothing to undo";
        update_project_dirty_state(state);
        return false;
    }

    update_project_dirty_state(state);
    state->status_message = "Undid " + label;
    return true;
}

bool redo_project_change(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    std::string label;
    if (!state->command_stack.redo(state, &label)) {
        state->status_message =
            state->command_stack.can_redo() ? "Redo failed" : "Nothing to redo";
        update_project_dirty_state(state);
        return false;
    }

    update_project_dirty_state(state);
    state->status_message = "Redid " + label;
    return true;
}

void handle_project_history_shortcuts(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        undo_project_change(state);
        return;
    }

    if (ImGui::Shortcut(
            ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z,
            ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal)) {
        redo_project_change(state);
    }

    // Space: Play/Pause
    if (ImGui::Shortcut(ImGuiKey_Space, ImGuiInputFlags_RouteGlobal)) {
        if (selected_animation(*state) != nullptr) {
            state->timeline_playing = !state->timeline_playing;
            if (state->timeline_playing) {
                refresh_preview_pose(state);
            }
            state->status_message =
                std::string(state->timeline_playing ? "Playing " : "Paused ") +
                state->selected_animation_name;
        }
    }

    // Home: Reset to 0
    if (ImGui::Shortcut(ImGuiKey_Home, ImGuiInputFlags_RouteGlobal)) {
        state->timeline_playing = false;
        scrub_timeline_time(state, 0.0, "Shortcut", true);
    }

    // F: Frame skeleton to viewport
    if (ImGui::Shortcut(ImGuiKey_F, ImGuiInputFlags_RouteGlobal)) {
        const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        auto_frame_skeleton(state, canvas_size);
    }
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
    state->saved_project_snapshot =
        marrow::editor::serialize_project(*state->load_result.project);
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

bool apply_current_animation_state_to_preview(ShellState* state) {
    if (!state->load_result || !state->preview_skeleton || !state->animation_state) {
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

    state->preview_root_motion_delta = {};
    state->preview_root_motion_total = {};
    state->preview_events.clear();
    state->preview_skeleton->set_attachment_playback_time(state->timeline_time_seconds);
    state->animation_state->apply(*state->preview_skeleton);
    apply_preview_slot_overrides(state);
    state->error_message.clear();
    return true;
}

bool restore_preview_playback(
    ShellState* state,
    const marrow::runtime::AnimationStateSnapshot& snapshot) {
    if (!state->animation_state || !state->preview_skeleton || !state->load_result) {
        return false;
    }

    state->animation_state->restore_state(snapshot);

    if (const std::shared_ptr<marrow::runtime::TrackEntry> current =
            state->animation_state->get_current(0);
        current != nullptr && !current->is_empty &&
        state->load_result.skeleton_data->find_animation(current->animation_name) != nullptr) {
        state->selected_animation_name = current->animation_name;
        state->timeline_time_seconds = std::clamp(
            current->track_time,
            0.0,
            timeline_preview_duration(*state));
    }

    return apply_current_animation_state_to_preview(state);
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
            marrow::allocate_unique<marrow::runtime::AnimationState>(
                state->load_result.skeleton_data);
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

std::optional<marrow::runtime::ProfilerFrame> build_preview_profiler_frame(
    const ShellState& state) {
    if (!state.load_result || state.load_result.atlas_data.empty()) {
        return std::nullopt;
    }

    const auto& skeleton_data = *state.load_result.skeleton_data;
    marrow::runtime::ProfilerCapture profiler(true);
    profiler.begin_frame();
    bool render_ready = true;

    marrow::runtime::Skeleton scratch_skeleton(state.load_result.skeleton_data);
    const std::vector<std::string> preview_skin_names =
        normalize_preview_skin_names(skeleton_data, state.preview_skin_names);
    std::vector<std::string_view> skin_names;
    skin_names.reserve(preview_skin_names.size());
    for (const std::string& skin_name : preview_skin_names) {
        skin_names.push_back(skin_name);
    }
    if (!scratch_skeleton.set_skin_composition(skin_names)) {
        return std::nullopt;
    }

    const double preview_duration = timeline_preview_duration(state);
    const double sampled_time =
        preview_duration > 0.0 ? std::clamp(state.timeline_time_seconds, 0.0, preview_duration)
                               : 0.0;
    scratch_skeleton.set_attachment_playback_time(sampled_time);

    if (const marrow::runtime::AnimationData* animation = selected_animation(state)) {
        marrow::runtime::AnimationState scratch_animation_state(state.load_result.skeleton_data);
        marrow::runtime::profile_phase(
            &profiler,
            marrow::runtime::ProfilerPhase::Animation,
            [&]() {
                scratch_animation_state.clear_tracks();
                const bool primary_loop =
                    state.preview_queue_enabled ? false : state.timeline_loop;
                std::shared_ptr<marrow::runtime::TrackEntry> current =
                    scratch_animation_state.set_animation(
                        0,
                        animation->name,
                        primary_loop,
                        0.0);
                current->reverse = state.preview_reverse;
                current->alpha = 1.0;

                if (state.preview_queue_enabled) {
                    if (const auto* queued_animation = queued_preview_animation(state)) {
                        const std::optional<double> mix_duration =
                            state.preview_use_custom_mix_duration
                                ? std::optional<double>(state.preview_custom_mix_duration)
                                : std::nullopt;
                        std::shared_ptr<marrow::runtime::TrackEntry> queued_entry =
                            scratch_animation_state.add_animation(
                                0,
                                queued_animation->name,
                                false,
                                state.preview_queue_delay,
                                mix_duration);
                        queued_entry->reverse = state.preview_reverse;
                    }
                }

                constexpr double kPreviewStep = 1.0 / 60.0;
                double elapsed_time = 0.0;
                while ((elapsed_time + kPreviewStep) < (sampled_time - 1e-9)) {
                    scratch_animation_state.update(kPreviewStep);
                    elapsed_time += kPreviewStep;
                }
                const double final_step = sampled_time - elapsed_time;
                if (final_step > 1e-9) {
                    scratch_animation_state.update(final_step);
                }

                scratch_animation_state.apply_pose(scratch_skeleton);
            });
    }

    marrow::runtime::WorldTransformTimingBreakdown timing_breakdown;
    scratch_skeleton.update_world_transforms(
        marrow::runtime::PhysicsMode::Pose,
        &timing_breakdown);
    profiler.add_world_transform_timing(timing_breakdown);

    apply_preview_slot_overrides(state, &scratch_skeleton);

    marrow::runtime::profile_phase(
        &profiler,
        marrow::runtime::ProfilerPhase::Skinning,
        [&]() {
            for (std::size_t slot_index = 0;
                 slot_index < scratch_skeleton.slot_states().size();
                 ++slot_index) {
                const auto* attachment = scratch_skeleton.current_attachment(slot_index);
                if (attachment == nullptr ||
                    (attachment->kind != marrow::runtime::AttachmentKind::Mesh &&
                     attachment->kind != marrow::runtime::AttachmentKind::LinkedMesh)) {
                    continue;
                }

                if (!scratch_skeleton.evaluate_current_mesh_attachment(slot_index).has_value()) {
                    render_ready = false;
                    return;
                }
            }
        });
    if (!render_ready) {
        return std::nullopt;
    }

    marrow::runtime::profile_phase(
        &profiler,
        marrow::runtime::ProfilerPhase::Render,
        [&]() {
            const marrow::renderer::PreparedSceneResult scene_result =
                marrow::renderer::prepare_setup_pose_scene(
                    scratch_skeleton,
                    *state.load_result.atlas_data.front());
            if (!scene_result) {
                render_ready = false;
                return;
            }

            const marrow::renderer::PreparedSceneBatchSummary batch_summary =
                marrow::renderer::summarize_prepared_scene_batches(*scene_result.scene);
            if (!batch_summary) {
                render_ready = false;
                return;
            }

            marrow::runtime::ProfilerDrawStats draw_stats;
            draw_stats.skeleton_count = batch_summary.skeleton_count;
            draw_stats.draw_calls = batch_summary.draw_call_count;
            draw_stats.vertices = batch_summary.vertex_count;
            draw_stats.batch_merges = batch_summary.merged_draw_calls;
            draw_stats.break_reasons.texture_changes =
                batch_summary.break_reasons.texture_changes;
            draw_stats.break_reasons.blend_changes =
                batch_summary.break_reasons.blend_changes;
            draw_stats.break_reasons.clip_changes =
                batch_summary.break_reasons.clip_changes;
            draw_stats.break_reasons.shader_changes =
                batch_summary.break_reasons.shader_changes;
            profiler.add_draw_stats(draw_stats);
        });
    if (!render_ready) {
        return std::nullopt;
    }

    profiler.end_frame();
    return marrow::runtime::marrow_profiler_frame(profiler);
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

    if (!state->animation_state || !state->preview_skeleton || !state->load_result) {
        state->timeline_playing = false;
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

    state->preview_events.clear();
    state->preview_root_motion_delta = {};

    state->animation_state->update(delta_seconds);

    const std::optional<std::size_t> root_bone_index =
        preview_root_bone_index(*state->load_result.skeleton_data);
    if (root_bone_index.has_value()) {
        const marrow::runtime::RootMotionDelta delta =
            state->animation_state->extract_root_motion(0, *root_bone_index);
        state->preview_root_motion_delta = delta;
        state->preview_root_motion_total.x += delta.x;
        state->preview_root_motion_total.y += delta.y;
    }

    state->preview_skeleton->set_attachment_playback_time(state->timeline_time_seconds);
    state->animation_state->apply(*state->preview_skeleton);
    apply_preview_slot_overrides(state);

    state->preview_skeleton->update_physics(delta_seconds);
}

void advance_timeline_playback(ShellState* state, float delta_seconds) {
    advance_timeline_playback(state, static_cast<double>(delta_seconds));
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


bool reload_runtime_source_assets(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    std::optional<std::string> previous_selection_name;
    if (const auto selection_name = selected_bone_name(*state)) {
        previous_selection_name = std::string(*selection_name);
    }
    std::optional<std::string> previous_slot_name;
    if (state->selected_slot_index.has_value() &&
        *state->selected_slot_index < state->load_result.skeleton_data->slots().size()) {
        previous_slot_name =
            state->load_result.skeleton_data->slots()[*state->selected_slot_index].name;
    }

    const auto document_result = marrow::runtime::load_skeleton_document(
        state->load_result.project->resolved_skeleton_path());
    if (!document_result) {
        state->error_message = document_result.error->format();
        state->status_message = "Runtime asset hot-reload failed";
        return false;
    }

    std::vector<std::shared_ptr<const marrow::runtime::AtlasData>> atlas_data;
    atlas_data.reserve(state->load_result.project->resolved_atlas_paths().size());
    for (const auto& atlas_path : state->load_result.project->resolved_atlas_paths()) {
        const auto atlas_result = marrow::runtime::AtlasLoader::load(atlas_path);
        if (!atlas_result) {
            state->error_message = atlas_result.error->format();
            state->status_message = "Runtime asset hot-reload failed";
            return false;
        }
        atlas_data.push_back(atlas_result.atlas_data);
    }

    state->load_result.base_skeleton_document =
        marrow::allocate_shared<marrow::runtime::json::Document>(
            std::move(*document_result.document));
    state->load_result.atlas_data = std::move(atlas_data);

    if (!rebuild_project_runtime(state)) {
        state->status_message = "Runtime asset hot-reload failed";
        return false;
    }

    if (previous_selection_name.has_value()) {
        state->selected_bone_index =
            state->load_result.skeleton_data->find_bone_index(*previous_selection_name);
    }
    if (previous_slot_name.has_value()) {
        state->selected_slot_index =
            state->load_result.skeleton_data->find_slot_index(*previous_slot_name);
    }
    if (state->selected_slot_index.has_value()) {
        sync_attachment_selection_for_slot(state, *state->selected_slot_index);
    }
    validate_selected_constraint(state);

    state->status_message =
        "Hot-reloaded runtime assets: " + join_paths(current_runtime_asset_paths(*state));
    state->error_message.clear();
    return true;
}

RuntimeAssetPollOutcome poll_runtime_asset_changes(ShellState* state) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return RuntimeAssetPollOutcome::Unchanged;
    }

    const std::vector<std::filesystem::path> current_paths = current_runtime_asset_paths(*state);
    if (state->runtime_asset_watch_entries.size() != current_paths.size()) {
        reset_runtime_asset_watch(state);
        return RuntimeAssetPollOutcome::Unchanged;
    }

    for (std::size_t index = 0; index < current_paths.size(); ++index) {
        if (state->runtime_asset_watch_entries[index].path != current_paths[index]) {
            reset_runtime_asset_watch(state);
            return RuntimeAssetPollOutcome::Unchanged;
        }
    }

    const std::vector<RuntimeAssetWatchEntry> current_entries =
        capture_runtime_asset_watch_entries(*state);
    bool changed = false;
    for (std::size_t index = 0; index < current_entries.size(); ++index) {
        if (!runtime_asset_watch_entry_equal(
                current_entries[index],
                state->runtime_asset_watch_entries[index])) {
            changed = true;
            break;
        }
    }

    if (!changed) {
        return RuntimeAssetPollOutcome::Unchanged;
    }

    if (!reload_runtime_source_assets(state)) {
        return RuntimeAssetPollOutcome::Failed;
    }

    reset_runtime_asset_watch(state);
    return RuntimeAssetPollOutcome::Reloaded;
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
    state->command_stack.clear();
    state->pending_edit_action.reset();
    reset_weight_paint_stroke(state);
    state->project_dirty = false;
    state->saved_project_snapshot.clear();
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
    state->saved_project_snapshot =
        marrow::editor::serialize_project(*state->load_result.project);
    state->preview_skeleton =
        marrow::allocate_unique<marrow::runtime::Skeleton>(state->load_result.skeleton_data);
    state->animation_state =
        marrow::allocate_unique<marrow::runtime::AnimationState>(
            state->load_result.skeleton_data);
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
    reset_runtime_asset_watch(state);
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
        if (ImGui::MenuItem("Quit", nullptr, false, window != nullptr)) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem(
                "Undo",
                "Ctrl+Z",
                false,
                state->command_stack.can_undo())) {
            undo_project_change(state);
        }
        if (ImGui::MenuItem(
                "Redo",
                "Ctrl+Shift+Z / Ctrl+Y",
                false,
                state->command_stack.can_redo())) {
            redo_project_change(state);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        const bool viewport_settings_available =
            state->load_result && state->load_result.project != nullptr;
        const bool onion_skin_enabled = state->viewport.onion_skin.enabled;
        if (ImGui::MenuItem(
                "Onion Skinning",
                nullptr,
                onion_skin_enabled,
                viewport_settings_available)) {
            apply_onion_skin_edit(
                state,
                std::string(onion_skin_enabled ? "Disabled" : "Enabled") + " onion skinning",
                "viewport:onion-skin:enabled",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->enabled = !onion_skin_enabled;
                });
        }
        if (ImGui::MenuItem(
                "Performance HUD",
                nullptr,
                state->hud_overlay_enabled,
                viewport_settings_available)) {
            state->hud_overlay_enabled = !state->hud_overlay_enabled;
            if (!state->hud_overlay_enabled) {
                state->hud_overlay_frame.reset();
            }
        }
        ImGui::Separator();
        const auto toggle_debug_overlay_item =
            [&](const char* label,
                bool enabled,
                std::string_view group,
                auto mutate) {
                if (ImGui::MenuItem(label, nullptr, enabled, viewport_settings_available)) {
                    apply_debug_overlay_edit(
                        state,
                        std::string(enabled ? "Disabled " : "Enabled ") + label,
                        std::string(group),
                        false,
                        mutate);
                }
            };
        toggle_debug_overlay_item(
            "Bone Hierarchy",
            state->viewport.debug_overlay.bones,
            "viewport:debug-overlay:bones",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->bones = !settings->bones;
            });
        toggle_debug_overlay_item(
            "IK Constraints",
            state->viewport.debug_overlay.ik_constraints,
            "viewport:debug-overlay:ik",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->ik_constraints = !settings->ik_constraints;
            });
        toggle_debug_overlay_item(
            "Path Constraints",
            state->viewport.debug_overlay.path_constraints,
            "viewport:debug-overlay:path",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->path_constraints = !settings->path_constraints;
            });
        toggle_debug_overlay_item(
            "Physics Constraints",
            state->viewport.debug_overlay.physics_constraints,
            "viewport:debug-overlay:physics",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->physics_constraints = !settings->physics_constraints;
            });
        toggle_debug_overlay_item(
            "Mesh Wireframes",
            state->viewport.debug_overlay.mesh_wireframes,
            "viewport:debug-overlay:meshes",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->mesh_wireframes = !settings->mesh_wireframes;
            });
        toggle_debug_overlay_item(
            "Bounding Boxes",
            state->viewport.debug_overlay.bounding_boxes,
            "viewport:debug-overlay:bounds",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->bounding_boxes = !settings->bounding_boxes;
            });
        ImGui::Separator();
        if (ImGui::MenuItem("Zoom to Fit", "F", false, viewport_settings_available)) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            auto_frame_skeleton(state, avail);
        }
        if (ImGui::MenuItem("Reset Viewport", nullptr, false, viewport_settings_available)) {
            state->viewport.zoom = 1.0;
            state->viewport.pan_x = 0.0;
            state->viewport.pan_y = 0.0;
            state->status_message = "Reset viewport to 1:1";
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Reset Layout")) {
            state->default_dock_layout_initialized = false;
            state->status_message = "Reset dock layout";
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts")) {
            state->status_message =
                "Shortcuts: Ctrl+Z Undo | Ctrl+Shift+Z Redo | Space Play/Pause | "
                "Home Reset | F Frame | RMB Pan | Wheel Zoom";
        }
        ImGui::Separator();
        ImGui::TextDisabled("Maroow Editor v1.0");
        ImGui::TextDisabled("Spine 4.2 compatible");
        ImGui::EndMenu();
    }

    ImGui::Separator();
    ImGui::TextUnformatted(state->status_message.c_str());
    ImGui::EndMainMenuBar();
}

void draw_project_window(bool* reload_requested, ShellState* state) {
    ImGui::Begin(kProjectWindowTitle);

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
    ImGui::Text(
        "History: undo %zu  redo %zu",
        state->command_stack.undo_count(),
        state->command_stack.redo_count());
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
    ImGui::Begin(kRuntimeAssetsWindowTitle);

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
    ImGui::Begin(kConstraintsWindowTitle);

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

    const auto constraint_group = [&](ConstraintEditKind kind, std::string_view name) {
        return std::string("constraint:") + constraint_kind_label(kind) + ":" +
            std::string(name);
    };
    const auto commit_constraint_change = [&](const marrow::editor::ProjectData& previous_project,
                                              ConstraintEditKind kind,
                                              std::string_view name,
                                              std::string failure_message,
                                              std::string success_message,
                                              bool allow_merge = true) {
        if (!apply_project_command_change(
                state,
                previous_project,
                EditActionKind::EditProperty,
                std::move(success_message),
                constraint_group(kind, name),
                allow_merge,
                std::move(failure_message))) {
            return false;
        }

        select_constraint(state, kind, name, "", false);
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
                const bool mix_changed = ImGui::SliderScalar(
                    "Mix",
                    ImGuiDataType_Double,
                    &edited_mix,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_coalesced_project_drag(
                    state,
                    mix_changed,
                    EditActionKind::EditProperty,
                    "Updated IK mix on " + selected_name,
                    constraint_group(ConstraintEditKind::Ik, selected_name),
                    false,
                    "IK constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_ik_constraint_edit_index(state, selected_name)) {
                            project->ik_constraint_edits[*edit_index].mix = edited_mix;
                        }
                    });

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
                const bool position_changed = ImGui::SliderScalar(
                    "Position",
                    ImGuiDataType_Double,
                    &edited_position,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_coalesced_project_drag(
                    state,
                    position_changed,
                    EditActionKind::EditProperty,
                    "Updated path position on " + selected_name,
                    constraint_group(ConstraintEditKind::Path, selected_name),
                    false,
                    "Path constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].position = edited_position;
                        }
                    });

                double edited_spacing = display_edit.spacing;
                const bool spacing_changed = ImGui::SliderScalar(
                    "Spacing",
                    ImGuiDataType_Double,
                    &edited_spacing,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_coalesced_project_drag(
                    state,
                    spacing_changed,
                    EditActionKind::EditProperty,
                    "Updated path spacing on " + selected_name,
                    constraint_group(ConstraintEditKind::Path, selected_name),
                    false,
                    "Path constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].spacing = edited_spacing;
                        }
                    });

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
                const bool rotate_mix_changed = ImGui::SliderScalar(
                    "Rotate Mix",
                    ImGuiDataType_Double,
                    &edited_rotate_mix,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_coalesced_project_drag(
                    state,
                    rotate_mix_changed,
                    EditActionKind::EditProperty,
                    "Updated path rotate mix on " + selected_name,
                    constraint_group(ConstraintEditKind::Path, selected_name),
                    false,
                    "Path constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].rotate_mix =
                                edited_rotate_mix;
                        }
                    });

                double edited_translate_mix = display_edit.translate_mix;
                const bool translate_mix_changed = ImGui::SliderScalar(
                    "Translate Mix",
                    ImGuiDataType_Double,
                    &edited_translate_mix,
                    &kZero,
                    &kOne,
                    "%.2f");
                apply_coalesced_project_drag(
                    state,
                    translate_mix_changed,
                    EditActionKind::EditProperty,
                    "Updated path translate mix on " + selected_name,
                    constraint_group(ConstraintEditKind::Path, selected_name),
                    false,
                    "Path constraint edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_path_constraint_edit_index(state, selected_name)) {
                            project->path_constraint_edits[*edit_index].translate_mix =
                                edited_translate_mix;
                        }
                    });
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
                    const bool changed = ImGui::SliderScalar(
                        label,
                        ImGuiDataType_Double,
                        &edited_value,
                        &kZero,
                        &kOne,
                        "%.2f");
                    apply_coalesced_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        constraint_group(ConstraintEditKind::Transform, selected_name),
                        false,
                        "Transform constraint edit failed",
                        [&]() {
                            if (const auto edit_index =
                                    ensure_transform_constraint_edit_index(state, selected_name)) {
                                setter(&project->transform_constraint_edits[*edit_index], edited_value);
                            }
                        });
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
                    const bool changed = ImGui::DragScalar(
                        label,
                        ImGuiDataType_Double,
                        &edited_value,
                        0.1f,
                        nullptr,
                        nullptr,
                        "%.3f");
                    apply_coalesced_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        constraint_group(ConstraintEditKind::Transform, selected_name),
                        false,
                        "Transform constraint edit failed",
                        [&]() {
                            if (const auto edit_index =
                                    ensure_transform_constraint_edit_index(state, selected_name)) {
                                setter(&project->transform_constraint_edits[*edit_index], edited_value);
                            }
                        });
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
                    const bool changed = ImGui::SliderScalar(
                        label,
                        ImGuiDataType_Double,
                        &edited_value,
                        &kZero,
                        &max_value,
                        "%.2f");
                    apply_coalesced_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        constraint_group(ConstraintEditKind::Physics, selected_name),
                        false,
                        "Physics constraint edit failed",
                        [&]() {
                            if (const auto edit_index =
                                    ensure_physics_constraint_edit_index(state, selected_name)) {
                                setter(&project->physics_constraint_edits[*edit_index], edited_value);
                            }
                        });
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
                                              float value,
                                              auto setter,
                                              std::string status) {
                    float edited_value = value;
                    const bool changed = ImGui::DragScalar(
                        label,
                        ImGuiDataType_Float,
                        &edited_value,
                        0.5f,
                        nullptr,
                        nullptr,
                        "%.2f");
                    apply_coalesced_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        constraint_group(ConstraintEditKind::Physics, selected_name),
                        false,
                        "Physics constraint edit failed",
                        [&]() {
                            if (const auto edit_index =
                                    ensure_physics_constraint_edit_index(state, selected_name)) {
                                setter(&project->physics_constraint_edits[*edit_index], edited_value);
                            }
                        });
                };
                update_force(
                    "Gravity X",
                    display_edit.gravity.x,
                    [](marrow::editor::PhysicsConstraintEdit* edit, float value) {
                        edit->gravity.x = value;
                    },
                    "Updated physics gravity X on " + selected_name);
                update_force(
                    "Gravity Y",
                    display_edit.gravity.y,
                    [](marrow::editor::PhysicsConstraintEdit* edit, float value) {
                        edit->gravity.y = value;
                    },
                    "Updated physics gravity Y on " + selected_name);
                update_force(
                    "Wind X",
                    display_edit.wind.x,
                    [](marrow::editor::PhysicsConstraintEdit* edit, float value) {
                        edit->wind.x = value;
                    },
                    "Updated physics wind X on " + selected_name);
                update_force(
                    "Wind Y",
                    display_edit.wind.y,
                    [](marrow::editor::PhysicsConstraintEdit* edit, float value) {
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
    ImGui::Begin(kHierarchyWindowTitle);

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
            apply_coalesced_project_drag(
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
            apply_coalesced_project_drag(
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
                apply_coalesced_project_drag(
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
                    EditActionKind::AddKeyframe,
                    track_group(),
                    false,
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
            if (display_edit.keyframes.size() > 1U &&
                ImGui::Button("Remove Key##transform")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index =
                        ensure_transform_timeline_edit_index(state, *track)) {
                    auto& editable_track =
                        state->load_result.project->transform_timeline_edits[*edit_index];
                    editable_track.keyframes.erase(
                        editable_track.keyframes.begin() + static_cast<std::ptrdiff_t>(key_index));
                    commit_project_change(
                        previous_project,
                        EditActionKind::RemoveKeyframe,
                        track_group(),
                        false,
                        "Removed a " +
                            std::string(transform_channel_label(*track->transform_channel)) +
                            " key on " + bone_name);
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
            apply_coalesced_project_drag(
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
                double edited_angle = display_key.angle;
                const bool angle_changed = ImGui::DragScalar(
                    "Angle",
                    ImGuiDataType_Double,
                    &edited_angle,
                    0.1f,
                    nullptr,
                    nullptr,
                    "%.3f deg");
                apply_coalesced_project_drag(
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
                                .angle = edited_angle;
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
                apply_coalesced_project_drag(
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
                apply_coalesced_project_drag(
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
                    apply_coalesced_project_drag(
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
            apply_coalesced_project_drag(
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
                    apply_coalesced_project_drag(
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
                apply_coalesced_project_drag(
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
                apply_coalesced_project_drag(
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

void draw_timeline_window(ShellState* state) {
    ImGui::Begin(kTimelineWindowTitle);

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
            const char* track_prefix = "";
            if (track.id.find(":Rotate") != std::string::npos) { track_prefix = "[R] "; }
            else if (track.id.find(":Translate") != std::string::npos) { track_prefix = "[T] "; }
            else if (track.id.find(":Scale") != std::string::npos) { track_prefix = "[S] "; }
            else if (track.id.find(":Shear") != std::string::npos) { track_prefix = "[H] "; }
            else if (track.id.find(":Color") != std::string::npos) { track_prefix = "[C] "; }
            else if (track.id.find(":Attachment") != std::string::npos) { track_prefix = "[A] "; }
            else if (track.id.find(":deform:") != std::string::npos) { track_prefix = "[M] "; }
            else if (track.id.find("draw_order") != std::string::npos) { track_prefix = "[D] "; }
            else if (track.id.find("event") != std::string::npos) { track_prefix = "[E] "; }
            const std::string label =
                std::string(track_prefix) + track.label + " (" + std::to_string(track.key_times.size()) + ")";
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

void draw_viewport_pose_fallback(
    const std::vector<BoneCanvasNode>& bones,
    float joint_radius,
    std::optional<std::size_t> selected_bone,
    std::optional<std::size_t> hovered_bone,
    ImU32 active_line_color,
    ImU32 inactive_line_color,
    ImU32 selected_line_color,
    ImU32 active_fill_color,
    ImU32 inactive_fill_color,
    ImU32 hovered_fill_color,
    ImU32 selected_fill_color,
    ImU32 active_outline_color,
    ImU32 inactive_outline_color,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }

    for (const BoneCanvasNode& node : bones) {
        if (!node.parent_index.has_value() || *node.parent_index >= bones.size()) {
            continue;
        }

        const BoneCanvasNode& parent = bones[*node.parent_index];
        const bool selected =
            selected_bone.has_value() && *selected_bone == node.bone_index;
        const ImU32 line_color = selected
            ? selected_line_color
            : node.active ? active_line_color : inactive_line_color;
        draw_list->AddLine(
            parent.screen_position,
            node.screen_position,
            line_color,
            selected ? 3.0f : 2.0f);
    }

    for (const BoneCanvasNode& node : bones) {
        const bool selected =
            selected_bone.has_value() && *selected_bone == node.bone_index;
        const bool hovered_selection =
            hovered_bone.has_value() && *hovered_bone == node.bone_index;
        const float radius = joint_radius + (selected ? 2.0f : 0.0f);
        const ImU32 fill_color = selected
            ? selected_fill_color
            : hovered_selection ? hovered_fill_color
                                : node.active ? active_fill_color : inactive_fill_color;
        const ImU32 outline_color =
            node.active ? active_outline_color : inactive_outline_color;
        draw_list->AddCircleFilled(node.screen_position, radius, fill_color, 18);
        draw_list->AddCircle(node.screen_position, radius, outline_color, 18, 1.5f);
    }
}

ImU32 average_overlay_triangle_color(
    const MeshWeightOverlayVertex& a,
    const MeshWeightOverlayVertex& b,
    const MeshWeightOverlayVertex& c) {
    const ImVec4 color_a = mesh_weight_heatmap_color(a.weight);
    const ImVec4 color_b = mesh_weight_heatmap_color(b.weight);
    const ImVec4 color_c = mesh_weight_heatmap_color(c.weight);
    const ImVec4 average(
        (color_a.x + color_b.x + color_c.x) / 3.0f,
        (color_a.y + color_b.y + color_c.y) / 3.0f,
        (color_a.z + color_b.z + color_c.z) / 3.0f,
        (color_a.w + color_b.w + color_c.w) / 3.0f);
    return ImGui::ColorConvertFloat4ToU32(average);
}

void draw_mesh_weight_overlay_fallback(
    const ViewportLayout& layout,
    const MeshWeightOverlay& overlay,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }

    for (std::size_t triangle_index = 0; triangle_index + 2U < overlay.triangles.size();
         triangle_index += 3U) {
        const std::size_t a = overlay.triangles[triangle_index];
        const std::size_t b = overlay.triangles[triangle_index + 1U];
        const std::size_t c = overlay.triangles[triangle_index + 2U];
        if (a >= overlay.vertices.size() ||
            b >= overlay.vertices.size() ||
            c >= overlay.vertices.size()) {
            continue;
        }

        draw_list->AddTriangleFilled(
            overlay.vertices[a].screen_position,
            overlay.vertices[b].screen_position,
            overlay.vertices[c].screen_position,
            average_overlay_triangle_color(
                overlay.vertices[a],
                overlay.vertices[b],
                overlay.vertices[c]));
    }

    for (const MeshWeightOverlayVertex& vertex : overlay.vertices) {
        const ImU32 fill_color =
            ImGui::ColorConvertFloat4ToU32(mesh_weight_heatmap_color(vertex.weight, 0.84f));
        draw_list->AddCircleFilled(
            vertex.screen_position,
            std::clamp(layout.render_joint_radius * 0.75f, 4.0f, 8.0f),
            fill_color,
            14);
        draw_list->AddCircle(
            vertex.screen_position,
            std::clamp(layout.render_joint_radius * 0.75f, 4.0f, 8.0f),
            IM_COL32(18, 21, 25, 180),
            14,
            1.0f);
    }
}

void draw_debug_overlay_fallback(
    const DebugOverlayGeometry& overlay,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }

    for (const auto& line : overlay.lines) {
        draw_list->AddLine(line.start, line.end, line.color, line.thickness);
    }
    for (const auto& circle : overlay.circles) {
        if ((circle.fill_color & IM_COL32_A_MASK) != 0U) {
            draw_list->AddCircleFilled(circle.center, circle.radius, circle.fill_color, 18);
        }
        if ((circle.outline_color & IM_COL32_A_MASK) != 0U) {
            draw_list->AddCircle(
                circle.center,
                circle.radius,
                circle.outline_color,
                24,
                circle.outline_thickness);
        }
    }
}

void draw_viewport_fallback_scene(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list) {
    draw_list->AddRectFilled(
        layout.canvas_origin,
        layout.canvas_end,
        IM_COL32(18, 21, 25, 255),
        6.0f);
    draw_list->AddRect(
        layout.canvas_origin,
        layout.canvas_end,
        IM_COL32(56, 61, 69, 255),
        6.0f);

    const float grid_spacing = std::max(18.0f, 40.0f * static_cast<float>(state.viewport.zoom));
    for (float x = first_grid_line(layout.world_origin_screen.x, layout.canvas_origin.x, grid_spacing);
         x < layout.canvas_end.x;
         x += grid_spacing) {
        draw_list->AddLine(
            ImVec2(x, layout.canvas_origin.y),
            ImVec2(x, layout.canvas_end.y),
            IM_COL32(31, 35, 41, 255));
    }
    for (float y = first_grid_line(layout.world_origin_screen.y, layout.canvas_origin.y, grid_spacing);
         y < layout.canvas_end.y;
         y += grid_spacing) {
        draw_list->AddLine(
            ImVec2(layout.canvas_origin.x, y),
            ImVec2(layout.canvas_end.x, y),
            IM_COL32(31, 35, 41, 255));
    }

    draw_list->AddLine(
        ImVec2(layout.canvas_origin.x, layout.world_origin_screen.y),
        ImVec2(layout.canvas_end.x, layout.world_origin_screen.y),
        IM_COL32(189, 86, 37, 255),
        1.5f);
    draw_list->AddLine(
        ImVec2(layout.world_origin_screen.x, layout.canvas_origin.y),
        ImVec2(layout.world_origin_screen.x, layout.canvas_end.y),
        IM_COL32(204, 177, 110, 255),
        1.5f);

    for (const OnionSkinGhostPose& ghost_pose : ghost_poses) {
        draw_viewport_pose_fallback(
            ghost_pose.bones,
            layout.render_joint_radius * 0.9f,
            std::nullopt,
            std::nullopt,
            ghost_pose.line_color,
            ghost_pose.line_color,
            ghost_pose.line_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.outline_color,
            ghost_pose.outline_color,
            draw_list);
    }

    if (mesh_weight_overlay != nullptr) {
        draw_mesh_weight_overlay_fallback(layout, *mesh_weight_overlay, draw_list);
    }

    const DebugOverlayGeometry debug_overlay = build_debug_overlay_geometry(state, layout);
    draw_debug_overlay_fallback(debug_overlay, draw_list);

    if (state.viewport.debug_overlay.bones) {
        draw_viewport_pose_fallback(
            layout.bones,
            layout.render_joint_radius,
            state.selected_bone_index,
            hovered_bone,
            IM_COL32(214, 163, 76, 220),
            IM_COL32(111, 117, 125, 180),
            IM_COL32(247, 204, 114, 255),
            IM_COL32(208, 134, 57, 230),
            IM_COL32(98, 103, 110, 200),
            IM_COL32(226, 186, 97, 240),
            IM_COL32(247, 204, 114, 255),
            IM_COL32(33, 37, 41, 255),
            IM_COL32(48, 50, 54, 255),
            draw_list);
    }
}

void draw_viewport_annotations(
    const ShellState& state,
    const ViewportLayout& layout,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list) {
    if (!state.load_result) {
        return;
    }

    const auto& bones = state.load_result.skeleton_data->bones();
    if (state.viewport.debug_overlay.bones) {
        for (const BoneCanvasNode& node : layout.bones) {
            const bool selected =
                state.selected_bone_index.has_value() && *state.selected_bone_index == node.bone_index;
            const bool hovered_selection =
                hovered_bone.has_value() && *hovered_bone == node.bone_index;
            const float radius = layout.render_joint_radius + (selected ? 2.0f : 0.0f);
            if (selected || hovered_selection || layout.bones.size() <= 12U) {
                draw_list->AddText(
                    ImVec2(node.screen_position.x + radius + 6.0f, node.screen_position.y - 6.0f),
                    selected ? IM_COL32(247, 232, 191, 255) : IM_COL32(225, 212, 180, 220),
                    bones[node.bone_index].name.c_str());
            }
        }
    }

    if (state.selected_bone_index.has_value() &&
        *state.selected_bone_index < bones.size()) {
        std::ostringstream selection_stream;
        selection_stream << bones[*state.selected_bone_index].name
                         << "  pan(" << static_cast<int>(state.viewport.pan_x) << ", "
                         << static_cast<int>(state.viewport.pan_y) << ")"
                         << "  zoom " << state.viewport.zoom;
        draw_list->AddText(
            ImVec2(layout.canvas_origin.x + 14.0f, layout.canvas_origin.y + 12.0f),
            IM_COL32(244, 230, 197, 255),
            selection_stream.str().c_str());
    }

    const DebugOverlayGeometry debug_overlay = build_debug_overlay_geometry(state, layout);
    std::vector<const char*> enabled_layers;
    enabled_layers.reserve(6U);
    if (state.viewport.debug_overlay.bones) {
        enabled_layers.push_back("bones");
    }
    if (debug_overlay.stats.ik_constraint_count > 0U) {
        enabled_layers.push_back("ik");
    }
    if (debug_overlay.stats.path_constraint_count > 0U) {
        enabled_layers.push_back("path");
    }
    if (debug_overlay.stats.physics_constraint_count > 0U) {
        enabled_layers.push_back("physics");
    }
    if (debug_overlay.stats.mesh_attachment_count > 0U) {
        enabled_layers.push_back("meshes");
    }
    if (debug_overlay.stats.bounding_box_count > 0U) {
        enabled_layers.push_back("bounds");
    }
    if (!enabled_layers.empty()) {
        std::ostringstream debug_stream;
        debug_stream << "Debug overlay:";
        for (const char* layer : enabled_layers) {
            debug_stream << ' ' << layer;
        }
        draw_list->AddText(
            ImVec2(layout.canvas_origin.x + 14.0f, layout.canvas_origin.y + 30.0f),
            IM_COL32(210, 221, 232, 220),
            debug_stream.str().c_str());
    }

    if (state.hud_overlay_enabled && state.hud_overlay_frame.has_value()) {
        const std::vector<std::string> hud_lines =
            marrow::runtime::profiler_hud_lines(*state.hud_overlay_frame);
        float hud_y = layout.canvas_origin.y + 48.0f;
        if (!enabled_layers.empty()) {
            hud_y += 18.0f;
        }
        for (const std::string& line : hud_lines) {
            draw_list->AddText(
                ImVec2(layout.canvas_origin.x + 14.0f, hud_y),
                IM_COL32(227, 236, 205, 232),
                line.c_str());
            hud_y += 18.0f;
        }
    }

    if (mesh_weight_overlay != nullptr) {
        std::ostringstream overlay_stream;
        overlay_stream << "Weight heatmap: "
                       << mesh_weight_overlay->target.display_attachment_name
                       << " -> " << mesh_weight_overlay->target.source_skin_name
                       << "/" << mesh_weight_overlay->target.source_attachment_name
                       << "  " << weight_paint_mode_name(state.weight_paint.mode)
                       << "  radius " << static_cast<int>(state.weight_paint.radius_pixels)
                       << "  strength " << std::fixed << std::setprecision(2)
                       << state.weight_paint.strength;
        draw_list->AddText(
            ImVec2(layout.canvas_origin.x + 14.0f, layout.canvas_end.y - 48.0f),
            IM_COL32(233, 223, 199, 230),
            overlay_stream.str().c_str());
    }
}

void draw_viewport_window(ShellState* state) {
    ImGui::Begin(kViewportWindowTitle);
    const std::optional<MeshWeightPaintTarget> paint_target =
        state->load_result ? current_mesh_weight_paint_target(*state) : std::nullopt;
    const bool weight_tool_ready =
        state->weight_paint.enabled &&
        paint_target.has_value() &&
        state->selected_bone_index.has_value() &&
        state->load_result &&
        *state->selected_bone_index < state->load_result.skeleton_data->bones().size();
    const std::string preview_label =
        state->selected_animation_name.empty() ? std::string("Setup pose preview")
                                               : "Animation preview / " + state->selected_animation_name;
    ImGui::TextUnformatted(preview_label.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(
        "%s  RMB drag pan  Wheel zoom",
        weight_tool_ready ? "LMB brush weights" : "LMB select");
    if (!state->selected_animation_name.empty()) {
        ImGui::TextDisabled(
            "%s / %s",
            format_time_seconds(state->timeline_time_seconds).c_str(),
            format_time_seconds(timeline_preview_duration(*state)).c_str());
    }
    ImGui::Separator();

    // ── Viewport Toolbar ──
    if (state->load_result && state->preview_skeleton) {
        const ImVec2 pre_toolbar_avail = ImGui::GetContentRegionAvail();
        if (ImGui::SmallButton("Fit")) {
            auto_frame_skeleton(state, pre_toolbar_avail);
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("1:1")) {
            state->viewport.zoom = 1.0;
            state->viewport.pan_x = 0.0;
            state->viewport.pan_y = 0.0;
            state->status_message = "Reset viewport to 1:1";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        {
            bool bones_on = state->viewport.debug_overlay.bones;
            if (ImGui::SmallButton(bones_on ? "[B]" : " B ")) {
                apply_debug_overlay_edit(
                    state,
                    std::string(bones_on ? "Disabled" : "Enabled") + " bones",
                    "viewport:debug-overlay:bones",
                    false,
                    [bones_on](marrow::editor::DebugOverlaySettings* s) {
                        s->bones = !bones_on;
                    });
            }
        }
        ImGui::SameLine();
        {
            bool onion_on = state->viewport.onion_skin.enabled;
            if (ImGui::SmallButton(onion_on ? "[O]" : " O ")) {
                apply_onion_skin_edit(
                    state,
                    std::string(onion_on ? "Disabled" : "Enabled") + " onion skinning",
                    "viewport:onion-skin:enabled",
                    false,
                    [onion_on](marrow::editor::OnionSkinSettings* s) {
                        s->enabled = !onion_on;
                    });
            }
        }
        ImGui::SameLine();
        {
            bool hud_on = state->hud_overlay_enabled;
            if (ImGui::SmallButton(hud_on ? "[H]" : " H ")) {
                state->hud_overlay_enabled = !hud_on;
                if (hud_on) {
                    state->hud_overlay_frame.reset();
                }
            }
        }
    }

    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 32.0f || canvas_size.y < 32.0f) {
        ImGui::End();
        return;
    }

    const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
    const ViewportFramebufferSize framebuffer_size = viewport_framebuffer_size(
        canvas_size,
        ImGui::GetIO().DisplayFramebufferScale);
    bool use_framebuffer = state->viewport_renderer.available;
    if (use_framebuffer) {
        if (const auto error = ensure_viewport_framebuffer(
                &state->viewport_renderer,
                framebuffer_size.width,
                framebuffer_size.height)) {
            state->viewport_renderer.error_message = *error;
            use_framebuffer = false;
        }
    }

    if (use_framebuffer) {
        ImGui::Image(
            ImTextureRef(static_cast<ImTextureID>(state->viewport_renderer.color_texture)),
            canvas_size,
            kViewportImageUv0,
            kViewportImageUv1);
        ImGui::SetCursorScreenPos(canvas_origin);
    }
    ImGui::InvisibleButton(
        "viewport_canvas",
        canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
        const ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
        state->viewport.pan_x += static_cast<double>(mouse_delta.x);
        state->viewport.pan_y += static_cast<double>(mouse_delta.y);
    }

    if (hovered && std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
        const float zoom_factor = ImGui::GetIO().MouseWheel > 0.0f ? 1.1f : 0.9f;
        state->viewport.zoom = static_cast<double>(
            clamp_zoom(static_cast<float>(state->viewport.zoom) * zoom_factor));
    }

    const auto layout = build_viewport_layout(*state, canvas_origin, canvas_size);
    state->hud_overlay_frame =
        state->hud_overlay_enabled ? build_preview_profiler_frame(*state) : std::nullopt;
    std::optional<MeshWeightOverlay> mesh_weight_overlay =
        layout.has_value() && (state->weight_paint.enabled || state->weight_paint.show_heatmap)
            ? build_mesh_weight_overlay(*state, *layout)
            : std::nullopt;
    const bool brush_enabled =
        state->weight_paint.enabled &&
        mesh_weight_overlay.has_value() &&
        state->selected_bone_index.has_value() &&
        state->load_result &&
        *state->selected_bone_index < state->load_result.skeleton_data->bones().size();
    std::optional<std::size_t> hovered_bone;
    if (hovered && layout.has_value()) {
        hovered_bone = pick_bone_at_position(*layout, ImGui::GetIO().MousePos);
    }
    const std::vector<OnionSkinGhostPose> ghost_poses =
        layout.has_value() ? build_onion_skin_ghost_poses(*state, *layout)
                           : std::vector<OnionSkinGhostPose>{};
    if (state->weight_paint_stroke.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        finish_weight_paint_stroke(state);
    }
    if (brush_enabled &&
        hovered &&
        mesh_weight_overlay.has_value() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        begin_weight_paint_stroke(state, mesh_weight_overlay->target);
    }
    if (brush_enabled &&
        hovered &&
        mesh_weight_overlay.has_value() &&
        state->weight_paint_stroke.active &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float sample_spacing = std::max(state->weight_paint.radius_pixels * 0.18f, 6.0f);
        if (!state->weight_paint_stroke.has_last_sample ||
            std::sqrt(
                squared_distance(
                    ImGui::GetIO().MousePos,
                    state->weight_paint_stroke.last_sample_position)) >= sample_spacing) {
            if (apply_weight_paint_sample(state, *mesh_weight_overlay, ImGui::GetIO().MousePos) &&
                layout.has_value()) {
                state->weight_paint_stroke.last_sample_position = ImGui::GetIO().MousePos;
                state->weight_paint_stroke.has_last_sample = true;
                mesh_weight_overlay = build_mesh_weight_overlay(*state, *layout);
            }
        }
    }
    if (!brush_enabled &&
        hovered &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        hovered_bone.has_value()) {
        select_bone(state, *hovered_bone, "Viewport", true);
    }

    const ImVec2 canvas_end(
        canvas_origin.x + canvas_size.x,
        canvas_origin.y + canvas_size.y);
    const MeshWeightOverlay* rendered_overlay =
        state->weight_paint.show_heatmap && mesh_weight_overlay.has_value()
            ? &(*mesh_weight_overlay)
            : nullptr;
    if (use_framebuffer && layout.has_value()) {
        if (const auto error = render_viewport_framebuffer(
                *state,
                *layout,
                ghost_poses,
                hovered_bone,
                rendered_overlay,
                &state->viewport_renderer)) {
            state->viewport_renderer.error_message = *error;
            use_framebuffer = false;
        } else {
            state->viewport_renderer.error_message.clear();
        }
    }

    if (use_framebuffer) {
        draw_list->AddRect(canvas_origin, canvas_end, IM_COL32(56, 61, 69, 255), 6.0f);
    } else {
        if (layout.has_value()) {
            draw_viewport_fallback_scene(
                *state,
                *layout,
                ghost_poses,
                hovered_bone,
                rendered_overlay,
                draw_list);
        } else {
            draw_list->AddRectFilled(
                canvas_origin,
                canvas_end,
                IM_COL32(18, 21, 25, 255),
                6.0f);
            draw_list->AddRect(
                canvas_origin,
                canvas_end,
                IM_COL32(56, 61, 69, 255),
                6.0f);
        }
    }

    if (layout.has_value()) {
        draw_viewport_annotations(*state, *layout, hovered_bone, rendered_overlay, draw_list);
        if (brush_enabled && hovered) {
            draw_list->AddCircle(
                ImGui::GetIO().MousePos,
                state->weight_paint.radius_pixels,
                IM_COL32(248, 236, 211, 210),
                48,
                1.5f);
        }
    } else {
        draw_list->AddText(
            ImVec2(canvas_origin.x + 16.0f, canvas_origin.y + 16.0f),
            IM_COL32(240, 232, 213, 255),
            "Project load failed. Reload a valid .marrow file.");
    }

    if (hovered_bone.has_value() && state->load_result) {
        const auto& bones = state->load_result.skeleton_data->bones();
        ImGui::SetTooltip("%s", bones[*hovered_bone].name.c_str());
    }

    if (!state->viewport_renderer.error_message.empty()) {
        draw_list->AddText(
            ImVec2(canvas_origin.x + 14.0f, canvas_end.y - 28.0f),
            IM_COL32(228, 143, 104, 255),
            state->viewport_renderer.error_message.c_str());
    }

    ImGui::End();
}

void draw_viewport_settings(ShellState* state) {
    if (!state->load_result || !state->load_result.project) {
        return;
    }

    const std::optional<MeshWeightPaintTarget> paint_target =
        state->load_result ? current_mesh_weight_paint_target(*state) : std::nullopt;

    if (ImGui::CollapsingHeader("Onion Skin##settings")) {
        const auto& onion_skin = state->viewport.onion_skin;
        bool onion_enabled = onion_skin.enabled;
        if (ImGui::Checkbox("Enabled##onion_skin", &onion_enabled)) {
            apply_onion_skin_edit(
                state,
                std::string(onion_enabled ? "Enabled" : "Disabled") + " onion skinning",
                "viewport:onion-skin:enabled",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->enabled = onion_enabled;
                });
        }

        int mode_index = onion_skin.mode == marrow::editor::OnionSkinMode::Frame ? 0 : 1;
        constexpr const char* kOnionSkinModes[] = {"Frame", "Keyframe"};
        if (ImGui::Combo(
                "Mode##onion_skin",
                &mode_index,
                kOnionSkinModes,
                IM_ARRAYSIZE(kOnionSkinModes))) {
            apply_onion_skin_edit(
                state,
                std::string("Set onion skin mode to ") + kOnionSkinModes[mode_index],
                "viewport:onion-skin:mode",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->mode = mode_index == 0 ? marrow::editor::OnionSkinMode::Frame
                                                     : marrow::editor::OnionSkinMode::Keyframe;
                });
        }

        bool anchor_to_zero = onion_skin.anchor_to_zero;
        if (mode_index != 0) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Checkbox("Anchor To Frame 0##onion_skin", &anchor_to_zero)) {
            apply_onion_skin_edit(
                state,
                std::string(anchor_to_zero ? "Enabled" : "Disabled") + " onion-skin anchoring",
                "viewport:onion-skin:anchor",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->anchor_to_zero = anchor_to_zero;
                });
        }
        if (mode_index != 0) {
            ImGui::EndDisabled();
        }

        int before_count = onion_skin.before_count;
        const bool before_changed =
            ImGui::SliderInt("Before Ghosts##onion_skin", &before_count, 0, 6);
        apply_coalesced_onion_skin_drag(
            state,
            before_changed,
            "Updated onion-skin before ghost count",
            "viewport:onion-skin:before",
            false,
            [&](marrow::editor::OnionSkinSettings* settings) {
                settings->before_count = before_count;
            });

        int after_count = onion_skin.after_count;
        const bool after_changed =
            ImGui::SliderInt("After Ghosts##onion_skin", &after_count, 0, 6);
        apply_coalesced_onion_skin_drag(
            state,
            after_changed,
            "Updated onion-skin after ghost count",
            "viewport:onion-skin:after",
            false,
            [&](marrow::editor::OnionSkinSettings* settings) {
                settings->after_count = after_count;
            });

        int step = onion_skin.step;
        const char* step_label = mode_index == 0 ? "Frame Step##onion_skin"
                                                 : "Keyframe Stride##onion_skin";
        const bool step_changed = ImGui::SliderInt(step_label, &step, 1, 12);
        apply_coalesced_onion_skin_drag(
            state,
            step_changed,
            "Updated onion-skin sampling step",
            "viewport:onion-skin:step",
            false,
            [&](marrow::editor::OnionSkinSettings* settings) {
                settings->step = step;
            });

        ImGui::TextDisabled(
            "Before ghosts render cool blue, after ghosts render warm red.");
    }

    if (ImGui::CollapsingHeader("Debug Overlay##settings")) {
        const auto& debug_overlay = state->viewport.debug_overlay;
        const auto draw_toggle = [&](const char* label,
                                     const char* status_label,
                                     bool value,
                                     std::string_view group,
                                     auto mutate) {
            bool edited = value;
            if (ImGui::Checkbox(label, &edited)) {
                apply_debug_overlay_edit(
                    state,
                    std::string(edited ? "Enabled " : "Disabled ") + status_label,
                    std::string(group),
                    false,
                    [&](marrow::editor::DebugOverlaySettings* settings) {
                        mutate(settings, edited);
                    });
            }
        };

        draw_toggle(
            "Bones##debug_overlay", "bones",
            debug_overlay.bones, "viewport:debug-overlay:bones",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->bones = v; });
        draw_toggle(
            "IK Constraints##debug_overlay", "IK constraints",
            debug_overlay.ik_constraints, "viewport:debug-overlay:ik",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->ik_constraints = v; });
        draw_toggle(
            "Path Constraints##debug_overlay", "path constraints",
            debug_overlay.path_constraints, "viewport:debug-overlay:path",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->path_constraints = v; });
        draw_toggle(
            "Physics Constraints##debug_overlay", "physics constraints",
            debug_overlay.physics_constraints, "viewport:debug-overlay:physics",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->physics_constraints = v; });
        draw_toggle(
            "Mesh Wireframes##debug_overlay", "mesh wireframes",
            debug_overlay.mesh_wireframes, "viewport:debug-overlay:meshes",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->mesh_wireframes = v; });
        draw_toggle(
            "Bounding Boxes##debug_overlay", "bounding boxes",
            debug_overlay.bounding_boxes, "viewport:debug-overlay:bounds",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->bounding_boxes = v; });
    }

    if (ImGui::CollapsingHeader("Performance HUD##settings")) {
        bool hud_enabled = state->hud_overlay_enabled;
        if (ImGui::Checkbox("Enabled##performance_hud", &hud_enabled)) {
            state->hud_overlay_enabled = hud_enabled;
            if (!hud_enabled) {
                state->hud_overlay_frame.reset();
            }
        }
        ImGui::TextDisabled("Profiles animation, transforms, skinning, and render prep.");
    }

    if (ImGui::CollapsingHeader("Weight Paint##settings")) {
        bool tool_enabled = state->weight_paint.enabled;
        if (ImGui::Checkbox("Enable Tool##weight_paint", &tool_enabled)) {
            state->weight_paint.enabled = tool_enabled;
            if (!tool_enabled) {
                finish_weight_paint_stroke(state);
            }
        }

        int mode_index = 0;
        switch (state->weight_paint.mode) {
        case WeightPaintMode::Paint:  mode_index = 0; break;
        case WeightPaintMode::Erase:  mode_index = 1; break;
        case WeightPaintMode::Smooth: mode_index = 2; break;
        }
        constexpr const char* kWeightPaintModes[] = {"Paint", "Erase", "Smooth"};
        if (ImGui::Combo(
                "Mode##weight_paint",
                &mode_index,
                kWeightPaintModes,
                IM_ARRAYSIZE(kWeightPaintModes))) {
            state->weight_paint.mode =
                mode_index == 0 ? WeightPaintMode::Paint
                : mode_index == 1 ? WeightPaintMode::Erase
                                  : WeightPaintMode::Smooth;
        }

        ImGui::SliderFloat(
            "Radius##weight_paint", &state->weight_paint.radius_pixels,
            8.0f, 160.0f, "%.0f px");
        ImGui::SliderFloat(
            "Strength##weight_paint", &state->weight_paint.strength,
            0.05f, 1.0f, "%.2f");
        ImGui::Checkbox("Show Heat Map##weight_paint", &state->weight_paint.show_heatmap);

        if (paint_target.has_value()) {
            ImGui::Text("Preview mesh: %s", paint_target->display_attachment_name.c_str());
            ImGui::Text("Editing source: %s / %s",
                paint_target->source_skin_name.c_str(),
                paint_target->source_attachment_name.c_str());
        } else {
            ImGui::TextDisabled("Select a mesh slot to paint weights.");
        }

        if (state->selected_bone_index.has_value() &&
            *state->selected_bone_index < state->load_result.skeleton_data->bones().size()) {
            ImGui::Text("Active bone: %s",
                state->load_result.skeleton_data->bones()[*state->selected_bone_index].name.c_str());
        } else {
            ImGui::TextDisabled("Select a bone for paint target.");
        }
    }
}

void draw_inspector_window(ShellState* state) {
    ImGui::Begin(kPropertiesWindowTitle);

    if (!state->load_result || !state->preview_skeleton) {
        ImGui::TextUnformatted("Load a valid project to inspect setup-pose data.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    const auto children = build_bone_children(skeleton);
    const auto& world_transforms = state->preview_skeleton->bone_world_transforms();

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
            ImGui::Text(
                "Translate: (%.1f, %.1f)",
                static_cast<double>(setup_pose.x),
                static_cast<double>(setup_pose.y));
            ImGui::Text("Rotation: %.1f deg", static_cast<double>(setup_pose.rotation));
            ImGui::Text(
                "Scale: (%.2f, %.2f)",
                static_cast<double>(setup_pose.scale_x),
                static_cast<double>(setup_pose.scale_y));
            ImGui::Text(
                "Shear: (%.1f, %.1f)",
                static_cast<double>(setup_pose.shear_x),
                static_cast<double>(setup_pose.shear_y));
            const char* inherit_label = "normal";
            switch (bone.inherit) {
            case marrow::runtime::BoneInherit::Normal:
                inherit_label = "normal";
                break;
            case marrow::runtime::BoneInherit::OnlyTranslation:
                inherit_label = "onlyTranslation";
                break;
            case marrow::runtime::BoneInherit::NoRotationOrReflection:
                inherit_label = "noRotationOrReflection";
                break;
            case marrow::runtime::BoneInherit::NoScale:
                inherit_label = "noScale";
                break;
            case marrow::runtime::BoneInherit::NoScaleOrReflection:
                inherit_label = "noScaleOrReflection";
                break;
            }
            ImGui::Text("Inherit: %s", inherit_label);
            ImGui::Separator();
            ImGui::PushStyleColor(
                ImGuiCol_ChildBg, ImVec4(0.165f, 0.184f, 0.227f, 0.50f));
            ImGui::BeginChild(
                "local_pose_editor", ImVec2(0, 0),
                ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_FrameStyle);
            ImGui::TextUnformatted("Local Pose");
            ImGui::SameLine();
            ImGui::TextDisabled("(editable)");
            {
                auto& local_pose =
                    state->preview_skeleton->bone_poses()[bone_index].local_pose;
                bool pose_changed = false;
                float translate[2] = {local_pose.x, local_pose.y};
                if (ImGui::DragFloat2(
                        "Translate##bone_local",
                        translate,
                        1.0f,
                        0.0f,
                        0.0f,
                        "%.1f")) {
                    local_pose.x = translate[0];
                    local_pose.y = translate[1];
                    pose_changed = true;
                }
                float rotation = local_pose.rotation;
                if (ImGui::DragFloat(
                        "Rotation##bone_local",
                        &rotation,
                        0.5f,
                        -360.0f,
                        360.0f,
                        "%.1f deg")) {
                    local_pose.rotation = rotation;
                    pose_changed = true;
                }
                float scale[2] = {local_pose.scale_x, local_pose.scale_y};
                if (ImGui::DragFloat2(
                        "Scale##bone_local",
                        scale,
                        0.01f,
                        0.0f,
                        0.0f,
                        "%.3f")) {
                    local_pose.scale_x = scale[0];
                    local_pose.scale_y = scale[1];
                    pose_changed = true;
                }
                float shear[2] = {local_pose.shear_x, local_pose.shear_y};
                if (ImGui::DragFloat2(
                        "Shear##bone_local",
                        shear,
                        0.5f,
                        0.0f,
                        0.0f,
                        "%.1f")) {
                    local_pose.shear_x = shear[0];
                    local_pose.shear_y = shear[1];
                    pose_changed = true;
                }
                if (pose_changed) {
                    state->preview_skeleton->update_world_transforms();
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::Separator();
            ImGui::TextUnformatted("World Pose");
            ImGui::Text(
                "World position: (%.1f, %.1f)",
                static_cast<double>(world.world_x),
                static_cast<double>(world.world_y));
            ImGui::Text(
                "Basis X: (%.2f, %.2f)  Basis Y: (%.2f, %.2f)",
                static_cast<double>(world.a),
                static_cast<double>(world.c),
                static_cast<double>(world.b),
                static_cast<double>(world.d));
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
            auto& slot_state = state->preview_skeleton->slot_states()[slot_index];
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
            float light_color[4] = {
                slot_state.color.r,
                slot_state.color.g,
                slot_state.color.b,
                slot_state.color.a};
            if (ImGui::ColorEdit4(
                    "Light color##slot_color",
                    light_color,
                    ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                slot_state.color.r = light_color[0];
                slot_state.color.g = light_color[1];
                slot_state.color.b = light_color[2];
                slot_state.color.a = light_color[3];
            }
            if (slot_state.dark_color.has_value()) {
                float dark[4] = {
                    slot_state.dark_color->r,
                    slot_state.dark_color->g,
                    slot_state.dark_color->b,
                    slot_state.dark_color->a};
                if (ImGui::ColorEdit4(
                        "Dark tint##slot_dark",
                        dark,
                        ImGuiColorEditFlags_AlphaBar |
                            ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    slot_state.dark_color->r = dark[0];
                    slot_state.dark_color->g = dark[1];
                    slot_state.dark_color->b = dark[2];
                    slot_state.dark_color->a = dark[3];
                }
            } else {
                ImGui::Text("Dark tint: <none>");
            }
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

    draw_viewport_settings(state);

    ImGui::End();
}

void render_shell_frame(GLFWwindow* window, ShellState* shell_state) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    (void)poll_runtime_asset_changes(shell_state);
    advance_timeline_playback(shell_state, ImGui::GetIO().DeltaTime);
    handle_project_history_shortcuts(shell_state);

    bool reload_requested = false;
    draw_menu_bar(window, &reload_requested, shell_state);
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0U, main_viewport);
    ensure_default_dock_layout(shell_state, dockspace_id, main_viewport);
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
    glClearColor(0.063f, 0.075f, 0.098f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}


} // namespace marrow::editor::shell

int main(int argc, char** argv) {
    using namespace marrow::editor::shell;
    const ParseResult parse_result = parse_arguments(argc, argv);
    if (parse_result.status == ParseStatus::Help) {
        return 0;
    }
    if (parse_result.status != ParseStatus::Ok) {
        return 1;
    }

    const bool smoke_mode = parse_result.options.auto_close_frames.has_value();
#if defined(__APPLE__)
    if (parse_result.options.verify_launch_focus) {
        return run_launch_focus_verification();
    }
    if (smoke_mode) {
        return run_headless_smoke(parse_result.options);
    }
#elif !defined(__APPLE__)
    if (parse_result.options.verify_launch_focus) {
        std::cout << "Launch-focus verification is only supported on macOS.\n";
        return 0;
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

#if defined(__APPLE__)
    activate_editor_window_on_launch(window);
#endif

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
    load_editor_fonts();
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    ShellState shell_state;
    shell_state.project_path = parse_result.options.project_path;
    reload_project(&shell_state);
    if (const auto viewport_error =
            initialize_viewport_renderer(&shell_state.viewport_renderer)) {
        shell_state.viewport_renderer.error_message = *viewport_error;
    }

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

    destroy_viewport_renderer(&shell_state.viewport_renderer);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
