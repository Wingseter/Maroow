#pragma once

#include <cstdint>

#include "marrow/runtime/json.hpp"

namespace marrow::editor::shell {

struct ShellState;

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
     * @param state Live editor shell state to update.
     * @param cmd JSON command object: { "op": "...", "args": { ... } }
     * @return Dispatch result with success flag and optional error or state summary.
     */
    static AgentDispatchResult dispatch(ShellState* state, const runtime::json::Value& cmd);
    static runtime::json::Value result_to_json(AgentDispatchResult result);
};

} // namespace marrow::editor::shell
