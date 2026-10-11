#include "miniredis\tcp_client.hpp"
#include "miniredis\socket.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct BenchmarkResult {
    std::atomic_size_t successful_commands{0};
    std::atomic_size_t failed_commands{0};
    std::atomic_llong total_latency_microseconds{0};
};

bool parsePositive(const char* text, std::size_t& value) {
    try {
        std::size_t consumed = 0;
        const auto parsed = std::stoull(text, &consumed);
        if (consumed != std::string(text).size() || parsed == 0) return false;
        value = static_cast<std::size_t>(parsed);
        return true;
    } catch (...) {
        return false;
    }
}

bool parsePort(const char* text, std::uint16_t& port) {
    std::size_t value = 0;
    if (!parsePositive(text, value) || value > 65535) return false;
    port = static_cast<std::uint16_t>(value);
    return true;
}

void runWorker(std::size_t client_index, std::size_t requests_per_client,
               const std::string& host, std::uint16_t port, BenchmarkResult& result) {
    miniredis::TcpClient client;
    std::string error;
    const bool connected = client.connectTo(host, port, error);

    if (!connected) {
        result.failed_commands.fetch_add(requests_per_client * 2);
        return;
    }

    const std::string key = "bench:" + std::to_string(client_index);
    const std::string value = "value:" + std::to_string(client_index);
    for (std::size_t request = 0; request < requests_per_client; ++request) {
        for (const std::string& command : {"SET " + key + " " + value, "GET " + key}) {
            const auto begin = std::chrono::steady_clock::now();
            std::string reply;
            const bool success = client.sendCommand(command, reply, error);
            const auto end = std::chrono::steady_clock::now();
            const auto latency = std::chrono::duration_cast<std::chrono::microseconds>(end - begin).count();
            result.total_latency_microseconds.fetch_add(latency);
            if (success) {
                result.successful_commands.fetch_add(1);
            } else {
                result.failed_commands.fetch_add(1);
            }
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    std::uint16_t port = 6380;
    std::size_t clients = 1;
    std::size_t requests_per_client = 100;
    if ((argc > 2 && !parsePort(argv[2], port)) ||
        (argc > 3 && !parsePositive(argv[3], clients)) ||
        (argc > 4 && !parsePositive(argv[4], requests_per_client)) ||
        argc > 5) {
        std::cerr << "Usage: miniredis_benchmark [host] [tcp-port] [clients] [requests-per-client]\n";
        return EXIT_FAILURE;
    }

    if (clients > 256) {
        std::cerr << "clients must be between 1 and 256\n";
        return EXIT_FAILURE;
    }

    miniredis::SocketRuntime runtime;
    BenchmarkResult result;
    std::vector<std::thread> workers;
    workers.reserve(clients);
    const auto begin = std::chrono::steady_clock::now();
    for (std::size_t index = 0; index < clients; ++index) {
        workers.emplace_back(runWorker, index, requests_per_client, std::cref(host), port,
                             std::ref(result));
    }

    for (auto& worker : workers) worker.join();
    const auto end = std::chrono::steady_clock::now();
    const double elapsed_seconds = std::chrono::duration<double>(end - begin).count();
    const auto successful = result.successful_commands.load();
    const auto failed = result.failed_commands.load();
    const double throughput = elapsed_seconds > 0.0
                                  ? static_cast<double>(successful) / elapsed_seconds
                                  : 0.0;
    const double average_latency = successful == 0
                                       ? 0.0
                                       : static_cast<double>(result.total_latency_microseconds.load()) /
                                             static_cast<double>(successful);

    std::cout << "MiniRedis benchmark (actual local run)\n"
              << "host=" << host << " tcp_port=" << port
              << " clients=" << clients
              << " requests_per_client=" << requests_per_client << "\n"
              << "workload=SET+GET per request\n"
              << "successful_commands=" << successful << " failed_commands=" << failed << "\n"
              << std::fixed << std::setprecision(3)
              << "elapsed_seconds=" << elapsed_seconds << "\n"
              << "throughput_commands_per_second=" << throughput << "\n"
              << "average_command_latency_microseconds=" << average_latency << "\n";
    return failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
