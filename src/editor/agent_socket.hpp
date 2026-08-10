#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/runtime/json.hpp"

namespace marrow::editor::shell {

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
     * @brief Drains pending agent commands on the main-thread command context.
     * Must be called from the main UI thread.
     */
    std::size_t drain_commands(marrow::editor::AgentCommandContext& context);

private:
    void listen_loop(int port);

    std::thread thread_;
    std::atomic<bool> running_{false};
    static constexpr std::uintptr_t kInvalidSocketValue =
        std::numeric_limits<std::uintptr_t>::max();
    std::atomic<std::uintptr_t> server_socket_{kInvalidSocketValue};
    std::atomic<std::uintptr_t> client_socket_{kInvalidSocketValue};
    /**
     * Serializes stop()'s shutdown against the listener thread's close so a
     * descriptor can never be shut down after its number was recycled.
     */
    std::mutex socket_lifecycle_mutex_;
    std::string token_;

    std::mutex request_mutex_;
    std::queue<AgentCommandRequest> requests_;

    std::mutex response_mutex_;
    std::vector<AgentCommandResponse> responses_;
};

} // namespace marrow::editor::shell
