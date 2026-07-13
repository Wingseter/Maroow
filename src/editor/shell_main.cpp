#include <cstdio>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

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
#include "shell_asset_watch.hpp"
#include "shell_constraints.hpp"
#include "shell_agent_panel.hpp"
#include "shell_inspector.hpp"
#include "shell_project_panels.hpp"
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "shell_weight_paint.hpp"
#include "shell_viewport_ui.hpp"
#include "shell_theme.hpp"
#include "shell_state.hpp"
#include "viewport_renderer.hpp"
#include "agent_socket.hpp"
#include "marrow/editor/module.hpp"

namespace marrow::editor::shell {

// ── Editor Fonts (declared extern in shell_theme.hpp) ──
ImFont* g_font_regular = nullptr;
ImFont* g_font_semibold = nullptr;
ImFont* g_font_small = nullptr;
ImFont* g_font_display = nullptr;
ImFont* g_font_mono = nullptr;

void print_usage(std::string_view executable_name) {
    std::cout << "Usage: " << executable_name
              << " [project.marrow] [--auto-close <frames>] [--agent-port <port>] [--verify-launch-focus]\n"
                 "       "
              << executable_name
              << " --project <project.marrow> [--auto-close <frames>] "
                 "[--agent-port <port>] [--agent-token <secret>] "
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

        if (argument == "--agent-port") {
            if (index + 1 >= argc) {
                std::cerr << "--agent-port requires a port number.\n";
                print_usage(argv[0]);
                return result;
            }

            const std::optional<int> value = parse_positive_integer(argv[++index]);
            if (!value.has_value()) {
                std::cerr << "--agent-port expects a positive integer.\n";
                print_usage(argv[0]);
                return result;
            }

            result.options.agent_port = value;
            continue;
        }

        if (argument == "--agent-token") {
            if (index + 1 >= argc) {
                std::cerr << "--agent-token requires a value.\n";
                print_usage(argv[0]);
                return result;
            }

            result.options.agent_token = argv[++index];
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

void apply_editor_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    // Charcoal Studio v2 tokens — single source in shell_theme.hpp.
    namespace t = marrow::editor::shell::theme;
    const ImVec4 surface_lowest  = t::kSurfaceLowest;
    const ImVec4 surface         = t::kSurface;
    const ImVec4 surface_low     = t::kSurfaceLow;
    const ImVec4 surface_default = t::kSurfaceDefault;
    const ImVec4 surface_high    = t::kSurfaceHigh;
    const ImVec4 surface_highest = t::kSurfaceHighest;
    const ImVec4 surface_bright  = t::kSurfaceBright;
    const ImVec4 primary         = t::kPrimary;
    const ImVec4 primary_active  = t::kPrimaryContainer;
    const ImVec4 on_surface      = t::kOnSurface;
    const ImVec4 inactive        = t::kInactive;
    const ImVec4 outline_variant = t::kOutlineVariant;

    // Surface hierarchy (no-line rule: boundaries via tonal shift)
    c[ImGuiCol_WindowBg]           = surface;
    c[ImGuiCol_ChildBg]            = surface_low;
    c[ImGuiCol_PopupBg]            = ImVec4(surface_highest.x, surface_highest.y, surface_highest.z, 0.95f);
    c[ImGuiCol_MenuBarBg]          = surface_low;
    c[ImGuiCol_DockingEmptyBg]     = surface_lowest;

    // Header (tree / collapsing header)
    c[ImGuiCol_Header]             = surface_default;
    c[ImGuiCol_HeaderHovered]      = surface_high;
    c[ImGuiCol_HeaderActive]       = ImVec4(primary_active.x, primary_active.y, primary_active.z, 0.85f);

    // Button
    c[ImGuiCol_Button]             = surface_high;
    c[ImGuiCol_ButtonHovered]      = surface_bright;
    c[ImGuiCol_ButtonActive]       = primary_active;

    // Frame (input)
    c[ImGuiCol_FrameBg]            = surface_high;
    c[ImGuiCol_FrameBgHovered]     = surface_bright;
    c[ImGuiCol_FrameBgActive]      = ImVec4(primary_active.x, primary_active.y, primary_active.z, 0.67f);

    // Checkmark / slider
    c[ImGuiCol_CheckMark]          = primary_active;
    c[ImGuiCol_SliderGrab]         = primary;
    c[ImGuiCol_SliderGrabActive]   = primary_active;

    // Selection
    c[ImGuiCol_TextSelectedBg]     = ImVec4(primary_active.x, primary_active.y, primary_active.z, 0.35f);

    // Tab
    c[ImGuiCol_Tab]                = surface_low;
    c[ImGuiCol_TabHovered]         = surface_high;
    c[ImGuiCol_TabActive]          = surface_default;
    c[ImGuiCol_TabUnfocused]       = surface;
    c[ImGuiCol_TabUnfocusedActive] = surface_low;

    // Title bar
    c[ImGuiCol_TitleBg]            = surface_low;
    c[ImGuiCol_TitleBgActive]      = surface_default;
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(surface.x, surface.y, surface.z, 0.75f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]        = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_ScrollbarGrab]      = surface_highest;
    c[ImGuiCol_ScrollbarGrabHovered] = outline_variant;
    c[ImGuiCol_ScrollbarGrabActive]  = primary_active;

    // Separator (ghost border — 20% opacity of outline_variant)
    c[ImGuiCol_Separator]          = ImVec4(outline_variant.x, outline_variant.y, outline_variant.z, 0.30f);
    c[ImGuiCol_SeparatorHovered]   = ImVec4(primary_active.x, primary_active.y, primary_active.z, 0.78f);
    c[ImGuiCol_SeparatorActive]    = primary_active;
    c[ImGuiCol_Border]             = ImVec4(outline_variant.x, outline_variant.y, outline_variant.z, 0.20f);

    // Resize grip
    c[ImGuiCol_ResizeGrip]         = ImVec4(primary_active.x, primary_active.y, primary_active.z, 0.20f);
    c[ImGuiCol_ResizeGripHovered]  = ImVec4(primary_active.x, primary_active.y, primary_active.z, 0.67f);
    c[ImGuiCol_ResizeGripActive]   = ImVec4(primary_active.x, primary_active.y, primary_active.z, 0.95f);

    // Docking preview overlay
    c[ImGuiCol_DockingPreview]     = ImVec4(primary_active.x, primary_active.y, primary_active.z, 0.50f);

    // Text
    c[ImGuiCol_Text]               = on_surface;
    c[ImGuiCol_TextDisabled]       = inactive;

    // Table (v2 zebra: even rows a faint white lift, odd rows transparent)
    c[ImGuiCol_TableRowBg]         = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt]      = ImVec4(1.0f, 1.0f, 1.0f, 0.018f);
    c[ImGuiCol_TableBorderStrong]  = ImVec4(outline_variant.x, outline_variant.y, outline_variant.z, 0.40f);
    c[ImGuiCol_TableBorderLight]   = ImVec4(outline_variant.x, outline_variant.y, outline_variant.z, 0.20f);

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

// Toolbar-sized icon button. Returns true when clicked. When `active` is true,
// the button renders with the primary_active background (toggle-on state). When
// `disabled` is true, the button is desaturated and non-interactive.
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

    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;

    // Base body font MUST be added first — ImGui uses it as the default.
    if (!regular_path.empty()) {
        g_font_regular = io.Fonts->AddFontFromFileTTF(
            regular_path.c_str(), kBaseFontSize, &cfg);
    }
    if (!g_font_regular) {
        g_font_regular = io.Fonts->AddFontDefaultVector();
    }

    // Charcoal v2 type scale. Conservative sizes around the 15px base so the
    // existing layouts do not regress; helpers opt into these accents.
    if (!semibold_path.empty()) {
        g_font_semibold = io.Fonts->AddFontFromFileTTF(
            semibold_path.c_str(), kBaseFontSize, &cfg);
    }
    if (!g_font_semibold) {
        g_font_semibold = g_font_regular;
    }

    if (!regular_path.empty()) {
        g_font_small = io.Fonts->AddFontFromFileTTF(
            regular_path.c_str(), 12.0f, &cfg);
    }
    if (!g_font_small) {
        g_font_small = g_font_regular;
    }

    if (!semibold_path.empty()) {
        g_font_display = io.Fonts->AddFontFromFileTTF(
            semibold_path.c_str(), 22.0f, &cfg);
    }
    if (!g_font_display) {
        g_font_display = g_font_semibold;
    }

    // Data scale (coords / time). Prefer a bundled monospace if present;
    // otherwise reuse the body font (Pretendard renders tabular figures well).
    const char* kMonoCandidates[] = {
        "JetBrainsMono-Regular.ttf", "JetBrainsMono-Regular.otf",
        "IBMPlexMono-Regular.ttf",   "SpaceMono-Regular.ttf",
    };
    std::string mono_path;
    for (const char* name : kMonoCandidates) {
        mono_path = find_font_path(name);
        if (!mono_path.empty()) {
            break;
        }
    }
    if (!mono_path.empty()) {
        g_font_mono = io.Fonts->AddFontFromFileTTF(
            mono_path.c_str(), 14.0f, &cfg);
    }
    if (!g_font_mono) {
        g_font_mono = g_font_regular;
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
        state->dock_layout.layout_version == kDockLayoutVersion &&
        ImGui::DockBuilderGetNode(dockspace_id) != nullptr) {
        return;
    }

    state->dock_layout = {};
    state->dock_layout.dockspace_id = dockspace_id;

    ImGui::DockBuilderRemoveNode(dockspace_id);
    ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockspace_id, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);

    // Charcoal v2 shell. Two columns by default; the Agent column only exists
    // while the (optional) Agent panel is open, so closing it reclaims space.
    //   ┌ left ≈22% ┬ center (viewport / timeline) ┬ right ≈26% (Agent*) ┐
    //   │ Hier|Proj │ Viewport                     │ Agent  (* if open)  │
    //   │ Props|RT… │ Timeline                     │                     │
    ImGuiID dock_center_id = dockspace_id;
    ImGuiID dock_left_id = 0;
    ImGuiID dock_right_id = 0;
    ImGuiID dock_bottom_id = 0;
    ImGuiID dock_left_bottom_id = 0;
    ImGui::DockBuilderSplitNode(
        dock_center_id, ImGuiDir_Left, 0.22f, &dock_left_id, &dock_center_id);
    if (state->show_agent_panel) {
        // 0.26 of the original width = 0.333 of the remaining 0.78 node.
        ImGui::DockBuilderSplitNode(
            dock_center_id, ImGuiDir_Right, 0.333f, &dock_right_id,
            &dock_center_id);
    }
    ImGui::DockBuilderSplitNode(
        dock_center_id, ImGuiDir_Down, 0.30f, &dock_bottom_id,
        &dock_center_id);
    ImGui::DockBuilderSplitNode(
        dock_left_id, ImGuiDir_Down, 0.50f, &dock_left_bottom_id,
        &dock_left_id);

    state->dock_layout.viewport_node_id = dock_center_id;
    state->dock_layout.timeline_node_id = dock_bottom_id;
    state->dock_layout.hierarchy_node_id = dock_left_id;
    state->dock_layout.properties_node_id = dock_left_bottom_id;
    state->dock_layout.agent_node_id = dock_right_id;

    ImGui::DockBuilderDockWindow(kViewportWindowTitle, dock_center_id);
    ImGui::DockBuilderDockWindow(kTimelineWindowTitle, dock_bottom_id);
    ImGui::DockBuilderDockWindow(kHierarchyWindowTitle, dock_left_id);
    ImGui::DockBuilderDockWindow(kProjectWindowTitle, dock_left_id);
    ImGui::DockBuilderDockWindow(kPropertiesWindowTitle, dock_left_bottom_id);
    ImGui::DockBuilderDockWindow(kRuntimeAssetsWindowTitle, dock_left_bottom_id);
    ImGui::DockBuilderDockWindow(kConstraintsWindowTitle, dock_left_bottom_id);
    if (state->show_agent_panel) {
        ImGui::DockBuilderDockWindow(kAgentWindowTitle, dock_right_id);
    }
    ImGui::DockBuilderFinish(dockspace_id);

    state->dock_layout.layout_version = kDockLayoutVersion;
    state->default_dock_layout_initialized = true;
}

// 0 = setup pose, 1 = animation, 2 = weight paint. Derived from existing
// state (no new mode field) so the ModeStrip stays a faithful mirror.
void render_shell_frame(GLFWwindow* window, ShellState* shell_state) {
    sync_shell_from_editor_session_if_revised(shell_state);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    if (!authoring_gesture_active(*shell_state)) {
        (void)poll_runtime_asset_changes(shell_state);
    }
    advance_timeline_playback(shell_state, ImGui::GetIO().DeltaTime);
    handle_project_history_shortcuts(shell_state);

    bool reload_requested = false;
    draw_menu_bar(window, &reload_requested, shell_state);
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0U, main_viewport);
    ensure_default_dock_layout(shell_state, dockspace_id, main_viewport);

    // Mode environment wash — a barely-there full-viewport tint that shifts
    // with the working mode (Charcoal v2: setup=none, anim/paint=blue).
    {
        namespace t = marrow::editor::shell::theme;
        ImVec4 wash = t::kModeSetup;
        switch (current_shell_mode(shell_state)) {
            case ShellMode::Animation:   wash = t::kModeAnimation; break;
            case ShellMode::WeightPaint: wash = t::kModePaint; break;
            case ShellMode::Setup:       wash = t::kModeSetup; break;
        }
        if (wash.w > 0.0f) {
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                main_viewport->WorkPos,
                ImVec2(main_viewport->WorkPos.x + main_viewport->WorkSize.x,
                       main_viewport->WorkPos.y + main_viewport->WorkSize.y),
                t::u32(wash));
        }
    }
    draw_project_window(&reload_requested, shell_state);
    draw_runtime_window(*shell_state);
    draw_constraints_window(shell_state);
    draw_timeline_window(shell_state);
    draw_hierarchy_window(shell_state);
    draw_viewport_window(shell_state);
    draw_inspector_window(shell_state);
    // Agent panel is closed by default; toggling rebuilds the dock layout so
    // the column appears/disappears (no permanent empty slot when closed).
    if (shell_state->show_agent_panel != shell_state->agent_panel_was_open) {
        shell_state->agent_panel_was_open = shell_state->show_agent_panel;
        shell_state->default_dock_layout_initialized = false;
    }
    if (shell_state->show_agent_panel) {
        draw_agent_window(shell_state);
    }

    finalize_orphaned_inspector_transform_gesture(shell_state);
    finalize_orphaned_viewport_translate_gesture(shell_state);
    finalize_orphaned_edit_action(shell_state);

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
    shell_state.agent_listen_port = parse_result.options.agent_port;
    reload_project(&shell_state);
    if (const auto viewport_error =
            initialize_viewport_renderer(&shell_state.viewport_renderer)) {
        shell_state.viewport_renderer.error_message = *viewport_error;
    }

    AgentSocketServer agent_server;
    shell_state.agent_server = &agent_server;
    shell_state.agent_token = parse_result.options.agent_token;
    if (parse_result.options.agent_port.has_value()) {
        if (!agent_server.start(*parse_result.options.agent_port,
                                parse_result.options.agent_token)) {
            std::cerr << "Failed to start AI Agent server on port "
                      << *parse_result.options.agent_port << std::endl;
        }
        // Explicit --agent-port implies the user wants the panel visible.
        shell_state.show_agent_panel = true;
    }

    {
        std::error_code ec;
        std::filesystem::path icon_root = "icons";
        if (!std::filesystem::exists(icon_root, ec)) {
            icon_root = "assets/icons/export/white_48";
        }
        const int loaded = shell_state.icons.load_all(icon_root);
        if (loaded < static_cast<int>(marrow::editor::Icon::Count)) {
            std::fprintf(
                stderr,
                "[icon_registry] loaded %d/%d icons from %s\n",
                loaded,
                static_cast<int>(marrow::editor::Icon::Count),
                icon_root.string().c_str());
        }
    }

    int rendered_frames = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        marrow::editor::AgentCommandContext agent_context{
            shell_state.session,
            shell_state.agent_control};
        if (!authoring_gesture_active(shell_state) &&
            agent_server.drain_commands(agent_context) != 0U) {
            sync_shell_from_editor_session(&shell_state);
        }

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
