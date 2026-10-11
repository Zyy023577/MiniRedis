#include "miniredis/metrics.hpp"

#include <algorithm>
#include <cctype>

namespace miniredis {

void ServerMetrics::recordCommand(const std::string& command) {
    std::string normalized = command;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    std::lock_guard lock(mutex_);
    ++values_.total_commands;
    if (normalized == "GET") {
        ++values_.get_commands;
    } else if (normalized == "SET") {
        ++values_.set_commands;
    }
}

void ServerMetrics::recordGetResult(bool hit) {
    if (!hit) {
        return;
    }
    std::lock_guard lock(mutex_);
    ++values_.get_hits;
}

void ServerMetrics::clientConnected() {
    std::lock_guard lock(mutex_);
    ++values_.active_clients;
}

void ServerMetrics::clientDisconnected() {
    std::lock_guard lock(mutex_);
    if (values_.active_clients > 0) {
        --values_.active_clients;
    }
}

MetricsSnapshot ServerMetrics::snapshot() const {
    std::lock_guard lock(mutex_);
    return values_;
}

}  // namespace miniredis
