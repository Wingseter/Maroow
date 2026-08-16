#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace marrow::editor {

enum class AgentReviewKind {
    SaveProject,
    ExportRuntime,
    ImportOrPack,
};

struct AgentReviewRequest {
    std::uint64_t id{0};
    AgentReviewKind kind{AgentReviewKind::SaveProject};
    std::string op;
    std::string label;
    std::filesystem::path target_path;
    std::vector<std::filesystem::path> target_paths;
    std::string args_summary;
    bool binary_output{false};
    bool allowed{false};
    std::string message;
};

struct AgentActivityEntry {
    std::uint64_t id{0};
    std::string op;
    std::string category;
    bool ok{false};
    bool mutating{false};
    bool requires_review{false};
    std::string message;
};

/**
 * UI-independent state for one local agent-control session.
 *
 * Socket worker threads never mutate this object directly. Commands are queued
 * and dispatched by the editor's single writer, which keeps the review and
 * activity identifiers monotonic without synchronization in this type.
 */
struct AgentControlState {
    bool paused{false};
    bool terminated{false};
    std::string current_operation;
    std::string last_result;
    std::uint64_t next_activity_id{1};
    std::uint64_t next_review_id{1};
    std::vector<AgentActivityEntry> activity_log;
    std::vector<AgentReviewRequest> review_queue;
};

} // namespace marrow::editor
