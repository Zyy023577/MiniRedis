#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace miniredis {

struct MetricsSnapshot {
    std::uint64_t total_commands{0};
    std::uint64_t get_commands{0};
    std::uint64_t set_commands{0};
    std::uint64_t get_hits{0};
    std::uint64_t active_clients{0};
};

class ServerMetrics {
public:
    void recordCommand(const std::string& command);
    void recordGetResult(bool hit);
    void clientConnected();
    void clientDisconnected();
    MetricsSnapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    MetricsSnapshot values_;
};

}  // namespace miniredis
