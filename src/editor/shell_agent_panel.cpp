#include "shell_agent_panel.hpp"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>

#include "imgui.h"

#include "agent_socket.hpp"
#include "shell_theme.hpp"
#include "shell_widgets.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;

void draw_agent_window(ShellState* state) {
    namespace t = marrow::editor::shell::theme;
    // Pass the open flag so the window's [x] also toggles it off.
    if (!ImGui::Begin(kAgentWindowTitle, &state->show_agent_panel)) {
        ImGui::End();
        return;
    }

    widgets::panel_head(state->icons, Icon::Eye, "Agent");

    const bool running =
        state->agent_server != nullptr && state->agent_server->is_running();
    const int port = state->agent_listen_port.value_or(kDefaultAgentPort);
    std::string agent_status = "Off";
    if (state->agent_control.terminated) {
        agent_status = "Blocked";
    } else if (state->agent_control.paused) {
        agent_status = "Paused";
    } else if (!state->agent_control.current_operation.empty()) {
        agent_status = "Running";
    } else if (running && !state->agent_control.activity_log.empty()) {
        agent_status = state->agent_control.activity_log.back().ok ? "Connected" : "Error";
    } else if (running) {
        agent_status = "Listening";
    }

    // Status card (surface_card tonal lift, no border).
    {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, t::kSurfaceCard);
        ImGui::BeginChild("agent_conn", ImVec2(0.0f, 84.0f), true);
        ImGui::Dummy(ImVec2(0.0f, 2.0f));
        ImGui::TextColored(running ? t::kPrimary : t::kFaint, "●");
        ImGui::SameLine(0.0f, 8.0f);
        ImGui::BeginGroup();
        if (running) {
            char label[64];
            std::snprintf(label, sizeof(label), "%s · :%d", agent_status.c_str(), port);
            ImGui::TextColored(t::kOnSurface, "%s", label);
            ImGui::TextColored(t::kFaint,
                               state->agent_token.empty()
                                   ? "Localhost only · awaiting handshake."
                                   : "Localhost · token · awaiting handshake.");
        } else {
            ImGui::TextColored(t::kOnSurface, "%s", agent_status.c_str());
            ImGui::TextColored(t::kFaint,
                               "Start the socket to let an AI agent connect.");
        }
        if (!state->agent_control.current_operation.empty()) {
            ImGui::TextColored(
                t::kFaint, "Current: %s", state->agent_control.current_operation.c_str());
        } else if (!state->agent_control.last_result.empty()) {
            ImGui::TextColored(
                t::kFaint, "Last: %s", state->agent_control.last_result.c_str());
        }
        ImGui::EndGroup();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // Socket on/off control.
    ImGui::Dummy(ImVec2(0.0f, 8.0f));
    if (state->agent_server == nullptr) {
        ImGui::TextColored(t::kFaint, "Socket unavailable in this build.");
    } else if (running) {
        ImGui::PushStyleColor(ImGuiCol_Button, t::kStateErrBg);
        ImGui::PushStyleColor(ImGuiCol_Text, t::kTertiary);
        if (ImGui::Button("Stop socket", ImVec2(-1.0f, 0.0f))) {
            state->agent_server->stop();
            state->status_message = "Agent socket stopped";
        }
        ImGui::PopStyleColor(2);
    } else {
        ImGui::PushStyleColor(ImGuiCol_Button, t::kPrimaryContainer);
        ImGui::PushStyleColor(ImGuiCol_Text, t::kSurfaceLowest);
        char start_label[48];
        std::snprintf(start_label, sizeof(start_label),
                      "Start socket on :%d", port);
        if (ImGui::Button(start_label, ImVec2(-1.0f, 0.0f))) {
            if (state->agent_server->start(port, state->agent_token)) {
                state->agent_listen_port = port;
                state->status_message = "Agent socket listening";
            } else {
                state->status_message = "Agent socket failed to start";
            }
        }
        ImGui::PopStyleColor(2);
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    const std::string review_count_label = state->agent_control.review_queue.empty()
        ? "clear"
        : std::to_string(state->agent_control.review_queue.size());
    if (widgets::section_header("Review", review_count_label.c_str())) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, t::kSurfaceLow);
        ImGui::BeginChild("agent_review_queue", ImVec2(0.0f, 156.0f), false);
        if (state->agent_control.review_queue.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::TextColored(t::kFaint, "No pending file writes.");
        } else {
            std::optional<std::uint64_t> remove_review_id;
            for (const AgentReviewRequest& request : state->agent_control.review_queue) {
                ImGui::PushID(static_cast<int>(request.id));
                ImGui::TextColored(
                    request.allowed ? t::kOnSurface : t::kTertiary,
                    "#%llu %s",
                    static_cast<unsigned long long>(request.id),
                    request.label.c_str());
                if (!request.op.empty()) {
                    ImGui::TextColored(t::kFaint, "%s", request.op.c_str());
                }
                if (request.target_paths.empty()) {
                    ImGui::TextColored(t::kFaint, "%s", request.target_path.string().c_str());
                } else {
                    for (const auto& target : request.target_paths) {
                        ImGui::TextColored(t::kFaint, "%s", target.string().c_str());
                    }
                }
                if (!request.args_summary.empty()) {
                    ImGui::TextColored(t::kFaint, "%s", request.args_summary.c_str());
                }
                if (!request.message.empty()) {
                    ImGui::TextColored(
                        request.allowed ? t::kFaint : t::kTertiary,
                        "%s",
                        request.message.c_str());
                }
                if (request.allowed) {
                    if (ImGui::Button("Approve")) {
                        bool ok = false;
                        if (request.kind == AgentReviewKind::SaveProject) {
                            ok = save_project_file(state, true);
                        } else if (request.kind == AgentReviewKind::ExportRuntime) {
                            state->export_binary_output = request.binary_output;
                            ok = export_runtime_assets_file(state, true);
                        } else {
                            ok = true;
                            state->status_message = "Approved agent import/pack review #" +
                                std::to_string(request.id);
                        }
                        if (ok) {
                            state->status_message = "Approved agent request #" +
                                std::to_string(request.id);
                            remove_review_id = request.id;
                        }
                    }
                    ImGui::SameLine();
                }
                if (ImGui::Button("Reject")) {
                    state->status_message = "Rejected agent request #" +
                        std::to_string(request.id);
                    remove_review_id = request.id;
                }
                ImGui::Separator();
                ImGui::PopID();
            }
            if (remove_review_id.has_value()) {
                state->agent_control.review_queue.erase(
                    std::remove_if(
                        state->agent_control.review_queue.begin(),
                        state->agent_control.review_queue.end(),
                        [&](const AgentReviewRequest& request) {
                            return request.id == *remove_review_id;
                        }),
                    state->agent_control.review_queue.end());
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::Dummy(ImVec2(0.0f, 10.0f));
    if (widgets::section_header("Activity", running ? "live" : "off")) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, t::kSurfaceLow);
        ImGui::BeginChild("agent_feed", ImVec2(0.0f, 0.0f), false);
        if (state->agent_control.activity_log.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 8.0f));
            ImGui::TextColored(t::kFaint, "No agent operations yet.");
        } else {
            for (auto it = state->agent_control.activity_log.rbegin();
                 it != state->agent_control.activity_log.rend();
                 ++it) {
                ImGui::TextColored(
                    it->ok ? t::kPrimary : t::kTertiary,
                    "#%llu %s",
                    static_cast<unsigned long long>(it->id),
                    it->ok ? "OK" : "ERR");
                ImGui::SameLine(0.0f, 8.0f);
                ImGui::TextUnformatted(it->op.empty() ? "<invalid>" : it->op.c_str());
                if (it->mutating || it->requires_review) {
                    ImGui::SameLine();
                    ImGui::TextColored(
                        it->requires_review ? t::kPrimary : t::kFaint,
                        "%s",
                        it->requires_review ? "review" : "edit");
                }
                ImGui::TextColored(t::kFaint, "%s", it->message.c_str());
                ImGui::Separator();
            }
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImGui::End();
}


} // namespace marrow::editor::shell

