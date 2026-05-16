#pragma once

#include "marrow/runtime/json.hpp"

namespace marrow::editor::shell {

struct ShellState;

struct AgentDispatchResult {
    bool ok{false};
    std::string message;
    runtime::json::Value scene_delta;
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
};

} // namespace marrow::editor::shell
