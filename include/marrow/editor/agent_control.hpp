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
    /// @brief MAR-189. The reviewed input. Empty for save and export requests.
    std::filesystem::path input_path;
    /**
     * @brief MAR-189. A digest of the PLAN the reviewer saw. Empty when not applicable.
     *
     * Approval re-plans into a fresh staging root and refuses when this differs.
     * Holding a staging directory open across an unbounded human wait was rejected:
     * it leaks on reject, on quit and on crash, and it makes the review queue own a
     * filesystem lifetime it has no way to bound.
     */
    std::string plan_digest;
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
