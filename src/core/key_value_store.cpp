#include "miniredis/key_value_store.hpp"

#include <algorithm>
#include <mutex>

namespace miniredis {

KeyValueStore::KeyValueStore(std::size_t max_key_length, std::size_t max_value_length,
                             std::size_t max_key_count,
                             std::chrono::milliseconds cleanup_interval)
    : max_key_length_(max_key_length),
      max_value_length_(max_value_length),
      max_key_count_(std::max<std::size_t>(1, max_key_count)),
      cleanup_interval_(std::max(std::chrono::milliseconds(1), cleanup_interval)),
      cleanup_thread_(&KeyValueStore::cleanupLoop, this) {}

KeyValueStore::~KeyValueStore() {
    {
        std::unique_lock lock(mutex_);
        stopping_ = true;
    }
    cleanup_cv_.notify_one();
    if (cleanup_thread_.joinable()) {
        cleanup_thread_.join();
    }
}

bool KeyValueStore::validKey(const std::string& key, std::string& error) const {
    if (key.empty()) {
        error = "key must not be empty";
        return false;
    }
    if (key.size() > max_key_length_) {
        error = "key is too long";
        return false;
    }
    return true;
}

bool KeyValueStore::isExpired(const Entry& entry, Clock::time_point now) const {
    return entry.expires_at.has_value() && now >= *entry.expires_at;
}

void KeyValueStore::removeExpiredLocked(Clock::time_point now) {
    for (auto it = values_.begin(); it != values_.end();) {
        if (isExpired(it->second, now)) {
            lru_order_.erase(it->second.lru_position);
            it = values_.erase(it);
        } else {
            ++it;
        }
    }
}

void KeyValueStore::removeEntryLocked(std::unordered_map<std::string, Entry>::iterator it) {
    lru_order_.erase(it->second.lru_position);
    values_.erase(it);
}

void KeyValueStore::touchLocked(Entry& entry) {
    lru_order_.splice(lru_order_.begin(), lru_order_, entry.lru_position);
}

void KeyValueStore::enforceCapacityLocked() {
    while (values_.size() > max_key_count_) {
        const auto oldest_key = lru_order_.back();
        lru_order_.pop_back();
        values_.erase(oldest_key);
    }
}

void KeyValueStore::cleanupLoop() {
    std::unique_lock lock(mutex_);
    while (!stopping_) {
        if (cleanup_cv_.wait_for(lock, cleanup_interval_, [this] { return stopping_; })) {
            break;
        }
        removeExpiredLocked(Clock::now());
    }
}

bool KeyValueStore::set(const std::string& key, const std::string& value, std::string& error) {
    if (!validKey(key, error)) {
        return false;
    }
    if (value.size() > max_value_length_) {
        error = "value is too long";
        return false;
    }

    std::unique_lock lock(mutex_);
    removeExpiredLocked(Clock::now());
    const auto existing = values_.find(key);
    if (existing != values_.end()) {
        existing->second.value = value;
        existing->second.expires_at.reset();
        touchLocked(existing->second);
        return true;
    }

    lru_order_.push_front(key);
    values_.emplace(key, Entry{value, std::nullopt, lru_order_.begin()});
    enforceCapacityLocked();
    return true;
}

std::optional<std::string> KeyValueStore::get(const std::string& key) {
    std::unique_lock lock(mutex_);
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return std::nullopt;
    }
    if (isExpired(it->second, Clock::now())) {
        removeEntryLocked(it);
        return std::nullopt;
    }
    touchLocked(it->second);
    return it->second.value;
}

bool KeyValueStore::del(const std::string& key) {
    std::unique_lock lock(mutex_);
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return false;
    }
    if (isExpired(it->second, Clock::now())) {
        removeEntryLocked(it);
        return false;
    }
    removeEntryLocked(it);
    return true;
}

bool KeyValueStore::exists(const std::string& key) {
    std::unique_lock lock(mutex_);
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return false;
    }
    if (isExpired(it->second, Clock::now())) {
        removeEntryLocked(it);
        return false;
    }
    return true;
}

std::vector<std::string> KeyValueStore::keys() {
    std::unique_lock lock(mutex_);
    removeExpiredLocked(Clock::now());

    std::vector<std::string> result;
    result.reserve(values_.size());
    for (const auto& [key, entry] : values_) {
        result.push_back(key);
    }
    return result;
}

bool KeyValueStore::expire(const std::string& key, std::int64_t seconds, std::string& error) {
    if (!validKey(key, error)) {
        return false;
    }
    if (seconds < 0) {
        error = "seconds must be non-negative";
        return false;
    }

    std::unique_lock lock(mutex_);
    const auto it = values_.find(key);
    if (it == values_.end() || isExpired(it->second, Clock::now())) {
        if (it != values_.end()) {
            removeEntryLocked(it);
        }
        error = "key does not exist";
        return false;
    }
    it->second.expires_at = Clock::now() + std::chrono::seconds(seconds);
    return true;
}

std::int64_t KeyValueStore::ttl(const std::string& key) {
    std::unique_lock lock(mutex_);
    const auto it = values_.find(key);
    if (it == values_.end()) {
        return -2;
    }
    const auto now = Clock::now();
    if (isExpired(it->second, now)) {
        removeEntryLocked(it);
        return -2;
    }
    if (!it->second.expires_at.has_value()) {
        return -1;
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
        *it->second.expires_at - now);
    return remaining.count() < 0 ? 0 : remaining.count();
}

StoreInfo KeyValueStore::info() {
    std::unique_lock lock(mutex_);
    removeExpiredLocked(Clock::now());
    std::size_t estimated_memory = 0;
    for (const auto& [key, entry] : values_) {
        estimated_memory += key.size() + entry.value.size();
    }
    return StoreInfo{values_.size(), max_key_length_, max_value_length_, max_key_count_,
                     estimated_memory};
}

std::vector<StoreSnapshotEntry> KeyValueStore::snapshot() {
    std::unique_lock lock(mutex_);
    const auto now = Clock::now();
    removeExpiredLocked(now);

    std::vector<StoreSnapshotEntry> result;
    result.reserve(values_.size());
    for (const auto& [key, entry] : values_) {
        std::optional<std::int64_t> ttl_seconds;
        if (entry.expires_at.has_value()) {
            const auto remaining = std::chrono::duration_cast<std::chrono::seconds>(
                *entry.expires_at - now);
            ttl_seconds = remaining.count();
        }
        result.push_back(StoreSnapshotEntry{key, entry.value, ttl_seconds});
    }
    return result;
}

}  // namespace miniredis
