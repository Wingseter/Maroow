#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include "../editor/agent_socket.hpp"
#include "marrow/editor/agent_control.hpp"
#include "marrow/editor/session.hpp"

namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
using SocketLength = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;

void close_socket(SocketHandle socket) {
    if (socket != kInvalidSocket) {
        closesocket(socket);
    }
}

class NetworkRuntime {
public:
    NetworkRuntime() {
        WSADATA data{};
        available_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~NetworkRuntime() {
        if (available_) {
            WSACleanup();
        }
    }
    bool available() const noexcept { return available_; }

private:
    bool available_{false};
};
#else
using SocketHandle = int;
using SocketLength = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;

void close_socket(SocketHandle socket) {
    if (socket != kInvalidSocket) {
        ::close(socket);
    }
}

class NetworkRuntime {
public:
    bool available() const noexcept { return true; }
};
#endif

class SocketOwner {
public:
    explicit SocketOwner(SocketHandle socket = kInvalidSocket) : socket_(socket) {}
    ~SocketOwner() { close_socket(socket_); }
    SocketOwner(const SocketOwner&) = delete;
    SocketOwner& operator=(const SocketOwner&) = delete;

    SocketHandle get() const noexcept { return socket_; }
    bool valid() const noexcept { return socket_ != kInvalidSocket; }

private:
    SocketHandle socket_{kInvalidSocket};
};

std::uint16_t reserve_loopback_port() {
    SocketOwner probe(::socket(AF_INET, SOCK_STREAM, 0));
    if (!probe.valid()) {
        throw std::runtime_error("failed to create loopback port probe");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (::bind(
            probe.get(),
            reinterpret_cast<const sockaddr*>(&address),
            static_cast<SocketLength>(sizeof(address))) != 0) {
        throw std::runtime_error("failed to bind loopback port probe");
    }
    SocketLength address_length = static_cast<SocketLength>(sizeof(address));
    if (::getsockname(
            probe.get(),
            reinterpret_cast<sockaddr*>(&address),
            &address_length) != 0) {
        throw std::runtime_error("failed to inspect loopback port probe");
    }
    return ntohs(address.sin_port);
}

SocketOwner connect_with_retry(std::uint16_t port) {
    for (int attempt = 0; attempt < 200; ++attempt) {
        SocketHandle raw = ::socket(AF_INET, SOCK_STREAM, 0);
        if (raw == kInvalidSocket) {
            throw std::runtime_error("failed to create test client socket");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = htons(port);
        if (::connect(
                raw,
                reinterpret_cast<const sockaddr*>(&address),
                static_cast<SocketLength>(sizeof(address))) == 0) {
            return SocketOwner(raw);
        }
        close_socket(raw);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("agent listener did not accept connections in time");
}

void send_one_byte_at_a_time(SocketHandle socket, std::string_view bytes) {
    for (const char byte : bytes) {
        const int sent = ::send(socket, &byte, 1, 0);
        if (sent != 1) {
            throw std::runtime_error("partial-auth test send failed");
        }
    }
}

std::string receive_line(SocketHandle socket) {
    std::string result;
    std::array<char, 128U> buffer{};
    while (result.find('\n') == std::string::npos) {
        const int received = ::recv(
            socket,
            buffer.data(),
            static_cast<int>(buffer.size()),
            0);
        if (received <= 0) {
            throw std::runtime_error("test client did not receive a complete line");
        }
        result.append(buffer.data(), static_cast<std::size_t>(received));
    }
    return result;
}

void expect(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

void test_partial_auth_and_orderly_close() {
    const std::uint16_t port = reserve_loopback_port();
    marrow::editor::shell::AgentSocketServer server;
    expect(server.start(static_cast<int>(port), "split-token"),
           "server should start once");
    {
        SocketOwner client = connect_with_retry(port);
        send_one_byte_at_a_time(client.get(), "split-token\n");
        const std::string response = receive_line(client.get());
        expect(response.find("authenticated") != std::string::npos,
               "partial token should authenticate without changing framing");
    }
    server.stop();
    expect(!server.is_running(), "server should stop after orderly client close");
}

void test_unauthorized_client_is_rejected() {
    const std::uint16_t port = reserve_loopback_port();
    marrow::editor::shell::AgentSocketServer server;
    expect(server.start(static_cast<int>(port), "expected-token"),
           "server should start for unauthorized test");
    {
        SocketOwner client = connect_with_retry(port);
        send_one_byte_at_a_time(client.get(), "wrong-token\n");
        const std::string response = receive_line(client.get());
        expect(response.find("Unauthorized") != std::string::npos,
               "wrong token should preserve the existing authentication error");
    }
    server.stop();
}

void test_repeated_start_stop() {
    marrow::editor::shell::AgentSocketServer server;
    for (int iteration = 0; iteration < 20; ++iteration) {
        const std::uint16_t port = reserve_loopback_port();
        expect(server.start(static_cast<int>(port)),
               "each repeated lifecycle should start");
        SocketOwner client = connect_with_retry(port);
        server.stop();
        expect(!server.is_running(), "each repeated lifecycle should stop");
    }
}

void test_restart_discards_stale_requests() {
    // A command that arrives just before stop() must not be executed by a
    // later session: start() begins from empty request/response queues.
    marrow::editor::EditorSession session;
    marrow::editor::AgentControlState control;
    marrow::editor::AgentCommandContext context{session, control};

    marrow::editor::shell::AgentSocketServer server;
    const std::uint16_t port = reserve_loopback_port();
    expect(server.start(static_cast<int>(port)), "zombie test server should start");
    {
        SocketOwner client = connect_with_retry(port);
        const std::string_view command = "{\"op\":\"agent.pause\"}\n";
        const int sent = ::send(
            client.get(),
            command.data(),
            static_cast<int>(command.size()),
            0);
        expect(
            sent == static_cast<int>(command.size()),
            "zombie test send should complete");
        // Give the listener time to park the request before stopping.
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        server.stop();
    }
    expect(!server.is_running(), "zombie test server should stop");

    const std::uint16_t second_port = reserve_loopback_port();
    expect(
        server.start(static_cast<int>(second_port)),
        "zombie test server should restart");
    const std::size_t drained = server.drain_commands(context);
    server.stop();

    expect(drained == 0U, "restart must not execute commands from a previous session");
    expect(!control.paused, "a stale command must not mutate agent control state");
}

} // namespace

int main() {
    NetworkRuntime network;
    if (!network.available()) {
        std::cerr << "Agent socket tests: network runtime initialization failed\n";
        return 1;
    }

    try {
        test_partial_auth_and_orderly_close();
        std::cout << "PASS: partial auth and orderly close\n";
        test_unauthorized_client_is_rejected();
        std::cout << "PASS: unauthorized client rejection\n";
        test_repeated_start_stop();
        std::cout << "PASS: 20 repeated server lifecycles\n";
        test_restart_discards_stale_requests();
        std::cout << "PASS: restart discards stale requests\n";
    } catch (const std::exception& exception) {
        std::cerr << "Agent socket tests failed: " << exception.what() << '\n';
        return 1;
    }

    std::cout << "Agent socket tests: 4 cases passed\n";
    return 0;
}
