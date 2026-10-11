#pragma once

#include "miniredis/key_value_store.hpp"
#include "miniredis/metrics.hpp"
#include "miniredis/socket.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <thread>
#include <vector>

namespace miniredis {

class AofPersistence;

class TcpServer {
public:
    TcpServer(KeyValueStore& store, std::string host = "127.0.0.1",
              std::uint16_t port = 6380, std::size_t max_request_length = 8192,
              AofPersistence* persistence = nullptr, ServerMetrics* metrics = nullptr);
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    int run(std::string& error);
    void stop();
    std::size_t activeClientCount() const { return active_clients_.load(); }

private:
    void handleClient(NativeSocket client_socket);
    void registerClient(NativeSocket client_socket);
    void unregisterClient(NativeSocket client_socket);
    void closeActiveClients();

    KeyValueStore& store_;
    const std::string host_;
    const std::uint16_t port_;
    const std::size_t max_request_length_;
    AofPersistence* persistence_{nullptr};
    ServerMetrics* metrics_{nullptr};
    std::atomic_bool stop_requested_{false};
    std::atomic_size_t active_clients_{0};
    std::mutex clients_mutex_;
    std::vector<NativeSocket> active_sockets_;
    std::vector<std::thread> client_threads_;
};

}  // namespace miniredis
