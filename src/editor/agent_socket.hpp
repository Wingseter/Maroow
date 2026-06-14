#pragma once

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "marrow/runtime/json.hpp"

namespace marrow::editor::shell {

struct ShellState;

struct AgentCommandRequest {
    std::string id;
    marrow::runtime::json::Value command;
};

struct AgentCommandResponse {
    std::string id;
    marrow::runtime::json::Value result;
};

class AgentSocketServer {
public:
    AgentSocketServer();
    ~AgentSocketServer();

    /**
     * @brief Starts the agent socket listener on 127.0.0.1:<port>.
     * @param port TCP port to bind.
     * @param token Optional shared secret. When non-empty, the first line a
     *        client sends must be exactly this token or the connection is
     *        rejected.
     * @return false if already running.
     */
    bool start(int port, std::string token = "");
    void stop();

    /** @brief True while the listener thread is bound and accepting. */
    bool is_running() const noexcept { return running_.load(); }

    /**
     * @brief Drains pending agent commands and executes them on the shell state.
     * Must be called from the main UI thread.
     */
    void drain_commands(ShellState* state);

private:
    void listen_loop(int port);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> server_fd_{-1};
    std::string token_;

    std::mutex request_mutex_;
    std::queue<AgentCommandRequest> requests_;

    std::mutex response_mutex_;
    std::vector<AgentCommandResponse> responses_;
};

} // namespace marrow::editor::shell
