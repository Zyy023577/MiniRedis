#include "miniredis/tcp_server.hpp"

#include "miniredis/command_processor.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace miniredis {

namespace {

std::string lastSocketError() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

void closeNativeSocket(NativeSocket handle) {
#ifdef _WIN32
    ::closesocket(handle);
#else
    ::close(handle);
#endif
}

bool setReuseAddress(NativeSocket socket, std::string& error) {
    int enabled = 1;
#ifdef _WIN32
    const int result = ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                                    reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#else
    const int result = ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#endif
    if (result != 0) {
        error = "setsockopt(SO_REUSEADDR) failed: " + lastSocketError();
        return false;
    }
    return true;
}

}  // namespace

TcpServer::TcpServer(KeyValueStore& store, std::string host, std::uint16_t port,
                     std::size_t max_request_length, AofPersistence* persistence,
                     ServerMetrics* metrics)
    : store_(store),
      host_(std::move(host)),
      port_(port),
      max_request_length_(max_request_length),
      persistence_(persistence),
      metrics_(metrics) {}

TcpServer::~TcpServer() {
    stop();
    closeActiveClients();
    for (auto& thread : client_threads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void TcpServer::stop() {
    stop_requested_.store(true);
}

void TcpServer::registerClient(NativeSocket client_socket) {
    std::lock_guard lock(clients_mutex_);
    active_sockets_.push_back(client_socket);
    active_clients_.fetch_add(1);
    if (metrics_ != nullptr) {
        metrics_->clientConnected();
    }
}

void TcpServer::unregisterClient(NativeSocket client_socket) {
    std::lock_guard lock(clients_mutex_);
    const auto it = std::find(active_sockets_.begin(), active_sockets_.end(), client_socket);
    if (it != active_sockets_.end()) {
        active_sockets_.erase(it);
        active_clients_.fetch_sub(1);
        if (metrics_ != nullptr) {
            metrics_->clientDisconnected();
        }
    }
}

void TcpServer::closeActiveClients() {
    std::lock_guard lock(clients_mutex_);
    for (const auto socket : active_sockets_) {
        closeNativeSocket(socket);
    }
}

void TcpServer::handleClient(NativeSocket client_socket) {
    registerClient(client_socket);
    Socket client(client_socket);
    CommandProcessor commands(store_, persistence_, metrics_);
    std::string input;
    std::string receive_error;
    char buffer[1024];

    while (!stop_requested_.load()) {
        const int count = client.receive(buffer, sizeof(buffer), receive_error);
        if (count <= 0) {
            break;
        }
        input.append(buffer, static_cast<std::size_t>(count));
        if (input.size() > max_request_length_ * 2) {
            std::string ignored;
            client.sendAll("(error) ERR request is too long\n", ignored);
            break;
        }

        std::size_t newline = std::string::npos;
        while ((newline = input.find('\n')) != std::string::npos) {
            std::string line = input.substr(0, newline);
            input.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.size() > max_request_length_) {
                std::string ignored;
                client.sendAll("(error) ERR request is too long\n", ignored);
                client.close();
                unregisterClient(client_socket);
                return;
            }

            const auto reply = commands.execute(line);
            std::string send_error;
            if (!client.sendAll(reply.text + "\n", send_error) || reply.should_close) {
                client.close();
                unregisterClient(client_socket);
                return;
            }
        }
    }

    client.close();
    unregisterClient(client_socket);
}

int TcpServer::run(std::string& error) {
    try {
        SocketRuntime runtime;
        Socket listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!listener.valid()) {
            error = "socket creation failed: " + lastSocketError();
            return 1;
        }
        if (!setReuseAddress(listener.nativeHandle(), error)) {
            return 1;
        }

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port_);
#ifdef _WIN32
        const auto address_value = ::inet_addr(host_.c_str());
        if (address_value == INADDR_NONE) {
            error = "host must be an IPv4 address: " + host_;
            return 1;
        }
        address.sin_addr.s_addr = address_value;
#else
        if (::inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1) {
            error = "host must be an IPv4 address: " + host_;
            return 1;
        }
#endif
        if (::bind(listener.nativeHandle(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            error = "bind failed: " + lastSocketError();
            return 1;
        }
        if (::listen(listener.nativeHandle(), SOMAXCONN) != 0) {
            error = "listen failed: " + lastSocketError();
            return 1;
        }

        std::cout << "[server] event=tcp_listen host=" << host_ << " port=" << port_ << '\n';
        while (!stop_requested_.load()) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(listener.nativeHandle(), &read_set);
            timeval timeout{0, 200000};
#ifdef _WIN32
            const int result = ::select(0, &read_set, nullptr, nullptr, &timeout);
#else
            const int result = ::select(listener.nativeHandle() + 1, &read_set, nullptr, nullptr, &timeout);
#endif
            if (result < 0) {
                if (stop_requested_.load()) {
                    break;
                }
                error = "select failed: " + lastSocketError();
                return 1;
            }
            if (result == 0 || !FD_ISSET(listener.nativeHandle(), &read_set)) {
                continue;
            }

            sockaddr_in client_address{};
#ifdef _WIN32
            int address_length = sizeof(client_address);
#else
            socklen_t address_length = sizeof(client_address);
#endif
            const NativeSocket client_socket = ::accept(
                listener.nativeHandle(), reinterpret_cast<sockaddr*>(&client_address), &address_length);
            if (client_socket == kInvalidSocket) {
                if (!stop_requested_.load()) {
                    std::cerr << "accept failed: " << lastSocketError() << '\n';
                }
                continue;
            }
            client_threads_.emplace_back(&TcpServer::handleClient, this, client_socket);
        }

        listener.close();
        closeActiveClients();
        for (auto& thread : client_threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        return 0;
    } catch (const std::exception& exception) {
        error = exception.what();
        return 1;
    }
}

}  // namespace miniredis
