#include "miniredis/command_processor.hpp"
#include "miniredis/key_value_store.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

void testBasicCommands() {
    miniredis::KeyValueStore store;
    std::string error;
    expect(store.set("name", "ayu", error), "SET succeeds");
    expect(store.get("name") == std::optional<std::string>("ayu"), "GET returns value");
    expect(store.set("name", "updated", error), "duplicate SET succeeds");
    expect(store.get("name") == std::optional<std::string>("updated"), "duplicate SET updates value");
    expect(store.exists("name"), "EXISTS sees key");
    expect(store.del("name"), "DEL removes key");
    expect(!store.exists("name"), "deleted key is absent");
    expect(store.ttl("name") == -2, "TTL reports missing key");
}

void testLimitsAndTtl() {
    miniredis::KeyValueStore store(4, 5);
    std::string error;
    expect(!store.set("abcde", "value", error) && error == "key is too long", "key length limit");
    expect(!store.set("key", "123456", error) && error == "value is too long", "value length limit");
    expect(store.set("key", "value", error), "limited SET succeeds");
    expect(store.expire("key", 1, error), "EXPIRE succeeds");
    expect(store.ttl("key") >= 0, "TTL reports remaining seconds");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    expect(!store.exists("key"), "expired key is absent");
    expect(store.ttl("key") == -2, "expired key reports missing TTL");
}

void testBackgroundCleanup() {
    miniredis::KeyValueStore store(256, 4096, 10, std::chrono::milliseconds(20));
    std::string error;
    expect(store.set("temporary", "value", error), "background cleanup SET");
    expect(store.expire("temporary", 1, error), "background cleanup EXPIRE");
    std::this_thread::sleep_for(std::chrono::milliseconds(1250));
    expect(store.info().key_count == 0, "background thread removes expired key");
}

void testLruEviction() {
    miniredis::KeyValueStore store(256, 4096, 2, std::chrono::milliseconds(20));
    std::string error;
    expect(store.set("first", "1", error), "LRU first SET");
    expect(store.set("second", "2", error), "LRU second SET");
    expect(store.get("first").has_value(), "GET marks first key as recently used");
    expect(store.set("third", "3", error), "LRU third SET");
    expect(store.exists("first"), "recently used key survives eviction");
    expect(!store.exists("second"), "least recently used key is evicted");
    expect(store.exists("third"), "new key survives eviction");

    expect(store.set("first", "updated", error), "repeated SET updates LRU key");
    expect(store.set("fourth", "4", error), "fourth SET triggers another eviction");
    expect(!store.exists("third"), "oldest key is evicted after repeated SET");
    expect(store.get("first") == std::optional<std::string>("updated"),
           "repeated SET keeps updated value");
}

void testCommandProcessor() {
    miniredis::KeyValueStore store;
    miniredis::CommandProcessor commands(store);
    expect(commands.execute("SET name ayu").text == "OK", "command SET");
    expect(commands.execute("get name").text == "ayu", "command names are case-insensitive");
    expect(commands.execute("EXISTS name").text == "(integer) 1", "command EXISTS");
    expect(commands.execute("INFO").text.find("keys=1") != std::string::npos, "command INFO");
    expect(!commands.execute("UNKNOWN").success, "unknown command is an error");
    expect(!commands.execute("   ").success, "empty command is an error");
    expect(commands.execute("QUIT").should_close, "QUIT requests close");
}

void testConcurrentAccess() {
    miniredis::KeyValueStore store;
    std::vector<std::thread> workers;
    for (int worker = 0; worker < 4; ++worker) {
        workers.emplace_back([&store, worker] {
            for (int index = 0; index < 250; ++index) {
                const std::string key = "worker-" + std::to_string(worker) + "-" + std::to_string(index);
                std::string error;
                expect(store.set(key, "value", error), "concurrent SET");
                expect(store.get(key).has_value(), "concurrent GET");
            }
        });
    }
    for (auto& worker : workers) {
        worker.join();
    }
    expect(store.info().key_count == 1000, "concurrent writes retain all keys");
}

}  // namespace

int main() {
    testBasicCommands();
    testLimitsAndTtl();
    testBackgroundCleanup();
    testLruEviction();
    testCommandProcessor();
    testConcurrentAccess();
    std::cout << "PASS: MiniRedis core tests (6 suites)\n";
    return EXIT_SUCCESS;
}
