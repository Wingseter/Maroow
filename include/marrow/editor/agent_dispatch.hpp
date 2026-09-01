#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>

#include "marrow/editor/agent_control.hpp"
#include "marrow/runtime/json.hpp"

namespace marrow::editor {

class EditorSession;

struct AgentCommandContext {
    EditorSession& session;
    AgentControlState& control;
};

struct AgentOperationDescriptor {
    std::string_view name;
    std::string_view category;
    bool mutating{false};
    bool requires_review{false};
    bool dry_run_supported{false};
    bool has_handler{false};
};

struct AgentDispatchResult {
    bool ok{false};
    std::string message;
    runtime::json::Value scene_delta;
    std::string op;
    std::string category;
    bool mutating{false};
    std::string error_code;
    bool requires_review{false};
    runtime::json::Value review;
    std::uint64_t activity_id{0};
};

class AgentCommandDispatcher {
public:
    /**
     * @brief Dispatches a JSON command to mutate the editor state.
     * @param context UI-independent authoring and agent-control state.
     * @param cmd JSON command object: { "op": "...", "args": { ... } }
     * @return Dispatch result with success flag and optional error or state summary.
     */
    static AgentDispatchResult dispatch(
        AgentCommandContext& context,
        const runtime::json::Value& cmd);
    static runtime::json::Value result_to_json(AgentDispatchResult result);
};

/** Returns the metadata used by both dispatch and `operations.list`. */
const AgentOperationDescriptor* agent_operation_descriptors() noexcept;
/**
 * @brief Executes a queued review after the editor approves it.
 *
 * UI-free by construction, which is the only way this story is testable: the
 * approval button lives inside `draw_agent_window`, which the headless smoke
 * never draws. This function is what the tests drive; nothing proves the button
 * calls it.
 *
 * Re-plans into a fresh staging root and refuses when the plan differs from the
 * one that was reviewed, so approving a stale review cannot commit a bundle
 * nobody looked at.
 *
 * @param session Session whose bundle is replaced.
 * @param control Review queue the request is taken from.
 * @param review_id Identifier of the queued request.
 * @return The commit's outcome. The request is removed on success or on a
 *         definitive refusal, and left in place when nothing was decided.
 */
AgentDispatchResult apply_agent_review(
    EditorSession& session,
    AgentControlState& control,
    std::uint64_t review_id);

std::size_t agent_operation_descriptor_count() noexcept;
/** Validates unique names and non-null registered handlers. */
bool validate_agent_operation_registry(std::string* error_out = nullptr);

} // namespace marrow::editor
