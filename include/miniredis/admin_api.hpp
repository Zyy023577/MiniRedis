#pragma once

#include "miniredis/aof_persistence.hpp"
#include "miniredis/key_value_store.hpp"
#include "miniredis/metrics.hpp"
#include "miniredis/socket.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace miniredis {

class AdminApiServer {
public:
    AdminApiServer(KeyValueStore& store, AofPersistence* persistence, ServerMetrics* metrics,
                   std::string host = "127.0.0.1", std::uint16_t port = 8080,
                   std::size_t max_request_length = 65536,
                   std::string static_root = {});
    ~AdminApiServer();

    AdminApiServer(const AdminApiServer&) = delete;
    AdminApiServer& operator=(const AdminApiServer&) = delete;

    int run(std::string& error);
    void stop();

private:
    void handleClient(NativeSocket client_socket);
    void closeActiveClients();
    void registerClient(NativeSocket client_socket);
    void unregisterClient(NativeSocket client_socket);

    KeyValueStore& store_;
    AofPersistence* persistence_{nullptr};
    ServerMetrics* metrics_{nullptr};
    const std::string host_;
    const std::uint16_t port_;
    const std::size_t max_request_length_;
    const std::string static_root_;
    std::atomic_bool stop_requested_{false};
    std::mutex clients_mutex_;
    std::vector<NativeSocket> active_sockets_;
    std::vector<std::thread> client_threads_;
};

}  // namespace miniredis
