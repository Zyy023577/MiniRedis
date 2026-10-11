#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <list>
#include <optional>
#include <shared_mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace miniredis {

struct StoreInfo {
    std::size_t key_count{0};
    std::size_t max_key_length{0};
    std::size_t max_value_length{0};
    std::size_t max_key_count{0};
    std::size_t estimated_memory_bytes{0};
};

struct StoreSnapshotEntry {
    std::string key;
    std::string value;
    std::optional<std::int64_t> ttl_seconds;
};

class KeyValueStore {
public:
    explicit KeyValueStore(std::size_t max_key_length = 256,
                           std::size_t max_value_length = 4096,
                           std::size_t max_key_count = 10000,
                           std::chrono::milliseconds cleanup_interval = std::chrono::milliseconds(100));
    ~KeyValueStore();

    KeyValueStore(const KeyValueStore&) = delete;
    KeyValueStore& operator=(const KeyValueStore&) = delete;

    bool set(const std::string& key, const std::string& value, std::string& error);
    std::optional<std::string> get(const std::string& key);
    bool del(const std::string& key);
    bool exists(const std::string& key);
    std::vector<std::string> keys();
    bool expire(const std::string& key, std::int64_t seconds, std::string& error);
    std::int64_t ttl(const std::string& key);
    StoreInfo info();
    std::vector<StoreSnapshotEntry> snapshot();

private:
    using Clock = std::chrono::steady_clock;
    using LruIterator = std::list<std::string>::iterator;

    struct Entry {
        std::string value;
        std::optional<Clock::time_point> expires_at;
        LruIterator lru_position;
    };

    bool validKey(const std::string& key, std::string& error) const;
    bool isExpired(const Entry& entry, Clock::time_point now) const;
    void removeExpiredLocked(Clock::time_point now);
    void removeEntryLocked(std::unordered_map<std::string, Entry>::iterator it);
    void touchLocked(Entry& entry);
    void enforceCapacityLocked();
    void cleanupLoop();

    const std::size_t max_key_length_;
    const std::size_t max_value_length_;
    const std::size_t max_key_count_;
    const std::chrono::milliseconds cleanup_interval_;
    std::unordered_map<std::string, Entry> values_;
    std::list<std::string> lru_order_;
    mutable std::shared_mutex mutex_;
    std::condition_variable_any cleanup_cv_;
    bool stopping_{false};
    std::thread cleanup_thread_;
};

}  // namespace miniredis
