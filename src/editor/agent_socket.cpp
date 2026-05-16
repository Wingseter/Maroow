#include "agent_socket.hpp"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <sstream>

#include "shell_types.hpp"
#include "marrow/editor/agent_dispatch.hpp"

namespace marrow::editor::shell {

namespace json = marrow::runtime::json;

AgentSocketServer::AgentSocketServer() = default;

AgentSocketServer::~AgentSocketServer() {
    stop();
}

bool AgentSocketServer::start(int port, std::string token) {
    if (running_) return false;
    // Reap any thread left over from a previous run.
    if (thread_.joinable()) {
        thread_.join();
    }
    token_ = std::move(token);
    running_ = true;
    thread_ = std::thread(&AgentSocketServer::listen_loop, this, port);
    return true;
}

void AgentSocketServer::stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) {
            thread_.join();
        }
        return;
    }

    // Unblock a thread parked in accept()/read() by tearing down the socket.
    const int fd = server_fd_.exchange(-1);
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }

    if (thread_.joinable()) {
        thread_.join();
    }
}

void AgentSocketServer::drain_commands(ShellState* state) {
    std::queue<AgentCommandRequest> local_requests;
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        std::swap(local_requests, requests_);
    }

    while (!local_requests.empty()) {
        auto req = std::move(local_requests.front());
        local_requests.pop();

        auto dispatch_result = AgentCommandDispatcher::dispatch(state, req.command);

        json::Value::Object res_obj;
        res_obj.emplace("ok", json::Value(dispatch_result.ok, {}));
        res_obj.emplace("message", json::Value(dispatch_result.message, {}));
        res_obj.emplace("scene_delta", std::move(dispatch_result.scene_delta));

        json::Value::Object rpc_res;
        rpc_res.emplace("jsonrpc", json::Value(std::string("2.0"), {}));
        rpc_res.emplace("id", json::Value(req.id, {}));
        rpc_res.emplace("result", json::Value(std::move(res_obj), {}));

        std::lock_guard<std::mutex> lock(response_mutex_);
        responses_.push_back({req.id, json::Value(std::move(rpc_res), {})});
    }
}

void AgentSocketServer::listen_loop(int port) {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Agent socket creation failed." << std::endl;
        running_ = false;
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<uint16_t>(port));

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        std::cerr << "Agent socket bind failed on port " << port << std::endl;
        close(server_fd);
        running_ = false;
        return;
    }

    if (listen(server_fd, 1) < 0) {
        std::cerr << "Agent socket listen failed." << std::endl;
        close(server_fd);
        running_ = false;
        return;
    }

    server_fd_ = server_fd;
    std::cout << "AI Agent socket listening on 127.0.0.1:" << port << std::endl;

    while (running_) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &addr_len);
        if (client_fd < 0) {
            if (running_) std::cerr << "Agent socket accept failed." << std::endl;
            continue;
        }

        std::cout << "AI Agent connected." << std::endl;

        std::string buffer;
        char chunk[1024];
        bool authenticated = token_.empty();
        while (running_) {
            ssize_t n = read(client_fd, chunk, sizeof(chunk) - 1);
            if (n <= 0) break;
            chunk[n] = '\0';
            buffer += chunk;

            size_t newline_pos;
            while ((newline_pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, newline_pos);
                buffer.erase(0, newline_pos + 1);

                if (line.empty()) continue;

                if (!authenticated) {
                    if (line == token_) {
                        authenticated = true;
                        const std::string ack =
                            "{\"jsonrpc\":\"2.0\",\"result\":{\"ok\":true,"
                            "\"message\":\"authenticated\"},\"id\":\"auth\"}\n";
                        write(client_fd, ack.c_str(), ack.size());
                    } else {
                        const std::string err =
                            "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32001,"
                            "\"message\":\"Unauthorized\"},\"id\":null}\n";
                        write(client_fd, err.c_str(), err.size());
                        n = 0; // disconnect this client; server keeps listening
                        break;
                    }
                    continue;
                }

                auto parse_result = json::parse_document(line);
                if (!parse_result) {
                    std::string err = "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\"},\"id\":null}\n";
                    write(client_fd, err.c_str(), err.size());
                    continue;
                }

                const json::Value& root = parse_result.document->root;
                const json::Value* id_val = json::find_member(root, "id");
                std::string req_id = (id_val && id_val->is_string()) ? id_val->as_string() : "";

                {
                    std::lock_guard<std::mutex> lock(request_mutex_);
                    requests_.push({req_id, root});
                }

                // Wait for the main thread to drain and answer this request.
                bool responded = false;
                while (running_ && !responded) {
                    {
                        std::lock_guard<std::mutex> lock(response_mutex_);
                        auto it = std::find_if(responses_.begin(), responses_.end(),
                            [&](const AgentCommandResponse& r) { return r.id == req_id; });
                        if (it != responses_.end()) {
                            std::string res_json = json::serialize_compact(it->result) + "\n";
                            write(client_fd, res_json.c_str(), res_json.size());
                            responses_.erase(it);
                            responded = true;
                        }
                    }
                    if (!responded) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
            if (n == 0 && !authenticated) break;
        }
        close(client_fd);
        std::cout << "AI Agent disconnected." << std::endl;
    }

    const int fd = server_fd_.exchange(-1);
    if (fd >= 0) {
        close(fd);
    }
}

} // namespace marrow::editor::shell
