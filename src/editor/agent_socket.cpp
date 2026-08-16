#include "agent_socket.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "marrow/editor/agent_dispatch.hpp"

namespace marrow::editor::shell {

namespace {

namespace json = marrow::runtime::json;
constexpr std::uintptr_t kEncodedInvalidSocket =
    std::numeric_limits<std::uintptr_t>::max();

#if defined(_WIN32)
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

int socket_last_error() noexcept {
    return WSAGetLastError();
}

bool socket_error_interrupted(int error) noexcept {
    return error == WSAEINTR;
}

bool socket_error_would_block(int error) noexcept {
    return error == WSAEWOULDBLOCK;
}

void close_socket(SocketHandle socket) noexcept {
    if (socket != kInvalidSocket) {
        closesocket(socket);
    }
}

void shutdown_socket(SocketHandle socket) noexcept {
    if (socket != kInvalidSocket) {
        shutdown(socket, SD_BOTH);
    }
}

class WinsockRuntime {
public:
    WinsockRuntime() {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        available_ = result == 0 &&
            LOBYTE(data.wVersion) == 2 && HIBYTE(data.wVersion) == 2;
        if (result == 0 && !available_) {
            WSACleanup();
        }
    }

    ~WinsockRuntime() {
        if (available_) {
            WSACleanup();
        }
    }

    bool available() const noexcept { return available_; }

private:
    bool available_{false};
};

bool network_runtime_available() {
    static WinsockRuntime runtime;
    return runtime.available();
}
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;

int socket_last_error() noexcept {
    return errno;
}

bool socket_error_interrupted(int error) noexcept {
    return error == EINTR;
}

bool socket_error_would_block(int error) noexcept {
    return error == EAGAIN || error == EWOULDBLOCK;
}

void close_socket(SocketHandle socket) noexcept {
    if (socket != kInvalidSocket) {
        ::close(socket);
    }
}

void shutdown_socket(SocketHandle socket) noexcept {
    if (socket != kInvalidSocket) {
        ::shutdown(socket, SHUT_RDWR);
    }
}

bool network_runtime_available() noexcept {
    return true;
}
#endif

std::uintptr_t encode_socket(SocketHandle socket) noexcept {
    if (socket == kInvalidSocket) {
        return kEncodedInvalidSocket;
    }
    return static_cast<std::uintptr_t>(socket);
}

SocketHandle decode_socket(std::uintptr_t value) noexcept {
    if (value == kEncodedInvalidSocket) {
        return kInvalidSocket;
    }
    return static_cast<SocketHandle>(value);
}

class NativeSocket {
public:
    NativeSocket() = default;
    explicit NativeSocket(SocketHandle handle) noexcept : handle_(handle) {}
    ~NativeSocket() { reset(); }

    NativeSocket(const NativeSocket&) = delete;
    NativeSocket& operator=(const NativeSocket&) = delete;

    NativeSocket(NativeSocket&& other) noexcept : handle_(other.release()) {}
    NativeSocket& operator=(NativeSocket&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    bool valid() const noexcept { return handle_ != kInvalidSocket; }
    SocketHandle get() const noexcept { return handle_; }

    SocketHandle release() noexcept {
        const SocketHandle released = handle_;
        handle_ = kInvalidSocket;
        return released;
    }

    void reset(SocketHandle replacement = kInvalidSocket) noexcept {
        close_socket(handle_);
        handle_ = replacement;
    }

    void shutdown_and_close() noexcept {
        shutdown_socket(handle_);
        reset();
    }

private:
    SocketHandle handle_{kInvalidSocket};
};

bool send_all(
    SocketHandle socket,
    std::string_view bytes,
    const std::atomic<bool>& running) {
    std::size_t offset = 0U;
    while (offset < bytes.size() && running.load()) {
        const std::size_t remaining = bytes.size() - offset;
#if defined(_WIN32)
        const int request_size = static_cast<int>(std::min<std::size_t>(
            remaining,
            static_cast<std::size_t>(std::numeric_limits<int>::max())));
        const int sent = ::send(socket, bytes.data() + offset, request_size, 0);
#else
#if defined(MSG_NOSIGNAL)
        constexpr int send_flags = MSG_NOSIGNAL;
#else
        constexpr int send_flags = 0;
#endif
        const ssize_t sent =
            ::send(socket, bytes.data() + offset, remaining, send_flags);
#endif
        if (sent > 0) {
            offset += static_cast<std::size_t>(sent);
            continue;
        }
        if (sent == 0) {
            return false;
        }
        const int error = socket_last_error();
        if (socket_error_interrupted(error)) {
            continue;
        }
        if (socket_error_would_block(error)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        return false;
    }
    return offset == bytes.size();
}

enum class ReceiveResult {
    Data,
    OrderlyClose,
    Interrupted,
    WouldBlock,
    Error,
};

ReceiveResult receive_some(
    SocketHandle socket,
    char* buffer,
    std::size_t capacity,
    std::size_t* received) {
#if defined(_WIN32)
    const int request_size = static_cast<int>(std::min<std::size_t>(
        capacity,
        static_cast<std::size_t>(std::numeric_limits<int>::max())));
    const int result = ::recv(socket, buffer, request_size, 0);
#else
    const ssize_t result = ::recv(socket, buffer, capacity, 0);
#endif
    if (result > 0) {
        *received = static_cast<std::size_t>(result);
        return ReceiveResult::Data;
    }
    *received = 0U;
    if (result == 0) {
        return ReceiveResult::OrderlyClose;
    }
    const int error = socket_last_error();
    if (socket_error_interrupted(error)) {
        return ReceiveResult::Interrupted;
    }
    if (socket_error_would_block(error)) {
        return ReceiveResult::WouldBlock;
    }
    return ReceiveResult::Error;
}

} // namespace

AgentSocketServer::AgentSocketServer() = default;

AgentSocketServer::~AgentSocketServer() {
    stop();
}

bool AgentSocketServer::start(int port, std::string token) {
    if (running_.load() || !network_runtime_available()) {
        return false;
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    token_ = std::move(token);
    {
        // Requests parked by a previous session must never execute in this
        // one, and stale responses must never answer a new request.
        std::lock_guard<std::mutex> request_lock(request_mutex_);
        std::queue<AgentCommandRequest>().swap(requests_);
    }
    {
        std::lock_guard<std::mutex> response_lock(response_mutex_);
        responses_.clear();
    }
    running_ = true;
    thread_ = std::thread(&AgentSocketServer::listen_loop, this, port);
    return true;
}

void AgentSocketServer::stop() {
    running_ = false;

    {
        std::lock_guard<std::mutex> lifecycle_lock(socket_lifecycle_mutex_);
        // Closing the listening socket is the only portable way to unblock a
        // thread parked in accept() (shutdown() on a listening socket is a
        // no-op on BSD/macOS and fails on Windows). The listener detects the
        // exchanged slot and skips its own close.
        const std::uintptr_t encoded =
            server_socket_.exchange(kInvalidSocketValue);
        const SocketHandle server_socket = decode_socket(encoded);
        if (server_socket != kInvalidSocket) {
            shutdown_socket(server_socket);
            close_socket(server_socket);
        }

        // The connected client socket must only be shut down here, never
        // closed: the listener thread may be inside recv()/send() on it, and
        // closing would let the OS recycle the descriptor number for an
        // unrelated file mid-call. The listener observes the shutdown,
        // returns, and closes the descriptor it owns.
        const SocketHandle client_socket =
            decode_socket(client_socket_.load());
        if (client_socket != kInvalidSocket) {
            shutdown_socket(client_socket);
        }
    }

    if (thread_.joinable()) {
        thread_.join();
    }
}

std::size_t AgentSocketServer::drain_commands(
    marrow::editor::AgentCommandContext& context) {
    std::queue<AgentCommandRequest> local_requests;
    {
        std::lock_guard<std::mutex> lock(request_mutex_);
        std::swap(local_requests, requests_);
    }

    std::size_t dispatched_count = 0U;
    while (!local_requests.empty()) {
        auto request = std::move(local_requests.front());
        local_requests.pop();

        auto dispatch_result =
            marrow::editor::AgentCommandDispatcher::dispatch(context, request.command);

        json::Value::Object rpc_response;
        rpc_response.emplace("jsonrpc", json::Value(std::string("2.0"), {}));
        rpc_response.emplace("id", json::Value(request.id, {}));
        rpc_response.emplace(
            "result",
            marrow::editor::AgentCommandDispatcher::result_to_json(
                std::move(dispatch_result)));

        std::lock_guard<std::mutex> lock(response_mutex_);
        responses_.push_back(
            {request.id, json::Value(std::move(rpc_response), {})});
        ++dispatched_count;
    }
    return dispatched_count;
}

void AgentSocketServer::listen_loop(int port) {
    NativeSocket server(::socket(AF_INET, SOCK_STREAM, 0));
    if (!server.valid()) {
        std::cerr << "Agent socket creation failed." << std::endl;
        running_ = false;
        return;
    }

    int reuse_address = 1;
#if defined(_WIN32)
    ::setsockopt(
        server.get(),
        SOL_SOCKET,
        SO_REUSEADDR,
        reinterpret_cast<const char*>(&reuse_address),
        static_cast<int>(sizeof(reuse_address)));
#else
    ::setsockopt(
        server.get(),
        SOL_SOCKET,
        SO_REUSEADDR,
        &reuse_address,
        static_cast<socklen_t>(sizeof(reuse_address)));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<std::uint16_t>(port));

    if (::bind(
            server.get(),
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<SocketLength>(sizeof(address))) != 0) {
        std::cerr << "Agent socket bind failed on port " << port << std::endl;
        running_ = false;
        return;
    }

    if (::listen(server.get(), 1) != 0) {
        std::cerr << "Agent socket listen failed." << std::endl;
        running_ = false;
        return;
    }

    server_socket_ = encode_socket(server.get());
    std::cout << "AI Agent socket listening on 127.0.0.1:" << port << std::endl;

    while (running_.load()) {
        sockaddr_in client_address{};
        SocketLength address_length =
            static_cast<SocketLength>(sizeof(client_address));
        NativeSocket client(::accept(
            server.get(),
            reinterpret_cast<sockaddr*>(&client_address),
            &address_length));
        if (!client.valid()) {
            if (running_.load()) {
                const int error = socket_last_error();
                if (socket_error_interrupted(error) || socket_error_would_block(error)) {
                    continue;
                }
                std::cerr << "Agent socket accept failed." << std::endl;
                // Persistent failures such as EMFILE keep the listening
                // socket readable; without a backoff this loop spins a full
                // core while flooding stderr.
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            continue;
        }
#if defined(__APPLE__)
        int suppress_sigpipe = 1;
        ::setsockopt(
            client.get(),
            SOL_SOCKET,
            SO_NOSIGPIPE,
            &suppress_sigpipe,
            static_cast<socklen_t>(sizeof(suppress_sigpipe)));
#endif
        client_socket_ = encode_socket(client.get());

        std::cout << "AI Agent connected." << std::endl;

        std::string buffer;
        std::array<char, 1024U> chunk{};
        bool authenticated = token_.empty();
        bool disconnect_client = false;
        while (running_.load() && !disconnect_client) {
            std::size_t received = 0U;
            const ReceiveResult receive_result = receive_some(
                client.get(),
                chunk.data(),
                chunk.size(),
                &received);
            if (receive_result == ReceiveResult::Interrupted) {
                continue;
            }
            if (receive_result == ReceiveResult::WouldBlock) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (receive_result != ReceiveResult::Data) {
                break;
            }
            buffer.append(chunk.data(), received);

            std::size_t newline_position = std::string::npos;
            while (!disconnect_client &&
                   (newline_position = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0U, newline_position);
                buffer.erase(0U, newline_position + 1U);

                if (line.empty()) {
                    continue;
                }

                if (!authenticated) {
                    if (line == token_) {
                        authenticated = true;
                        constexpr std::string_view acknowledgement =
                            "{\"jsonrpc\":\"2.0\",\"result\":{\"ok\":true,"
                            "\"message\":\"authenticated\"},\"id\":\"auth\"}\n";
                        disconnect_client = !send_all(
                            client.get(), acknowledgement, running_);
                    } else {
                        constexpr std::string_view unauthorized =
                            "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32001,"
                            "\"message\":\"Unauthorized\"},\"id\":null}\n";
                        send_all(client.get(), unauthorized, running_);
                        disconnect_client = true;
                    }
                    continue;
                }

                auto parse_result = json::parse_document(line);
                if (!parse_result) {
                    constexpr std::string_view parse_error =
                        "{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,"
                        "\"message\":\"Parse error\"},\"id\":null}\n";
                    disconnect_client = !send_all(
                        client.get(), parse_error, running_);
                    continue;
                }

                const json::Value& root = parse_result.document->root;
                const json::Value* id_value = json::find_member(root, "id");
                const std::string request_id =
                    id_value != nullptr && id_value->is_string()
                    ? id_value->as_string()
                    : "";

                {
                    std::lock_guard<std::mutex> lock(request_mutex_);
                    requests_.push({request_id, root});
                }

                bool responded = false;
                while (running_.load() && !responded) {
                    std::string serialized_response;
                    {
                        std::lock_guard<std::mutex> lock(response_mutex_);
                        const auto response = std::find_if(
                            responses_.begin(),
                            responses_.end(),
                            [&](const AgentCommandResponse& candidate) {
                                return candidate.id == request_id;
                            });
                        if (response != responses_.end()) {
                            serialized_response =
                                json::serialize_compact(response->result) + "\n";
                            responses_.erase(response);
                            responded = true;
                        }
                    }
                    if (responded) {
                        disconnect_client = !send_all(
                            client.get(), serialized_response, running_);
                    } else {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
            }
        }
        {
            std::lock_guard<std::mutex> lifecycle_lock(socket_lifecycle_mutex_);
            std::uintptr_t expected_client = encode_socket(client.get());
            if (client_socket_.compare_exchange_strong(
                    expected_client,
                    kInvalidSocketValue)) {
                client.shutdown_and_close();
            } else {
                // stop() closed the client to unblock recv()/send().
                client.release();
            }
        }
        std::cout << "AI Agent disconnected." << std::endl;
    }

    {
        std::lock_guard<std::mutex> lifecycle_lock(socket_lifecycle_mutex_);
        std::uintptr_t expected = encode_socket(server.get());
        if (server_socket_.compare_exchange_strong(expected, kInvalidSocketValue)) {
            server.shutdown_and_close();
        } else {
            // stop() closed the listener to unblock accept(); avoid a second close.
            server.release();
        }
    }
}

} // namespace marrow::editor::shell
