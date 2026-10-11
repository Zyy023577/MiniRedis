#include "miniredis/admin_api.hpp"
#include "miniredis/aof_persistence.hpp"
#include "miniredis/tcp_server.hpp"

#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {
miniredis::TcpServer* g_server = nullptr;
miniredis::AdminApiServer* g_admin_api = nullptr;

void handleSignal(int) {
    if (g_server != nullptr) {
        g_server->stop();
    }
    if (g_admin_api != nullptr) {
        g_admin_api->stop();
    }
}

std::uint16_t parsePort(const char* text) {
    const auto value = std::strtoul(text, nullptr, 10);
    return value <= 65535 ? static_cast<std::uint16_t>(value) : 0;
}
}  // namespace

int main(int argc, char* argv[]) {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::uint16_t port = argc > 2 ? parsePort(argv[2]) : 6380;
    const std::string aof_path = argc > 3 ? argv[3] : "data/miniredis.aof";
    const std::uint16_t admin_port = argc > 4 ? parsePort(argv[4]) : 8080;
    const std::string static_root = argc > 5 ? argv[5] : "web/dist";
    if (port == 0 || admin_port == 0) {
        std::cerr << "Usage: miniredis_server [ipv4-host] [port] [aof-path] [admin-port] [static-root]\n";
        return EXIT_FAILURE;
    }

    miniredis::KeyValueStore store;
    miniredis::AofPersistence persistence(aof_path);
    miniredis::ServerMetrics metrics;
    std::string persistence_error;
    if (!persistence.replay(store, persistence_error)) {
        std::cerr << "AOF replay failed: " << persistence_error << '\n';
        return EXIT_FAILURE;
    }

    miniredis::TcpServer server(store, host, port, 8192, &persistence, &metrics);
    miniredis::AdminApiServer admin_api(store, &persistence, &metrics, "127.0.0.1", admin_port,
                                        65536, static_root);
    std::string admin_error;
    std::thread admin_thread([&] { admin_api.run(admin_error); });

    g_server = &server;
    g_admin_api = &admin_api;
    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::string error;
    const int result = server.run(error);
    admin_api.stop();
    if (admin_thread.joinable()) {
        admin_thread.join();
    }
    g_server = nullptr;
    g_admin_api = nullptr;
    if (result != 0) {
        std::cerr << "Server stopped with error: " << error << '\n';
        return EXIT_FAILURE;
    }
    if (!admin_error.empty()) {
        std::cerr << "Admin API stopped with error: " << admin_error << '\n';
        return EXIT_FAILURE;
    }
    std::cout << "[server] event=stopped\n";
    return EXIT_SUCCESS;
}
