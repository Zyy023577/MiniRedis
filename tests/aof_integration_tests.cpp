#include "miniredis/aof_persistence.hpp"
#include "miniredis/command_processor.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::string execute(miniredis::CommandProcessor& commands, const std::string& line) {
    const auto reply = commands.execute(line);
    expect(reply.success, line + " failed: " + reply.text);
    return reply.text;
}

}  // namespace

int main() {
    const auto path = std::filesystem::temp_directory_path() / "miniredis_studio_aof_test.aof";
    const auto temporary_path = std::filesystem::path(path.string() + ".tmp");
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(temporary_path, ignored);

    {
        miniredis::KeyValueStore store;
        miniredis::AofPersistence persistence(path.string());
        miniredis::CommandProcessor commands(store, &persistence);
        execute(commands, "SET name ayu");
        execute(commands, "SET name updated");
        execute(commands, "SET removed value");
        execute(commands, "DEL removed");
        execute(commands, "EXPIRE name 60");
        execute(commands, "AOF REWRITE");

        std::ifstream rewritten(path);
        std::stringstream content;
        content << rewritten.rdbuf();
        const std::string text = content.str();
        expect(text.find("SET name updated") != std::string::npos,
               "rewrite keeps the latest value");
        expect(text.find("SET name ayu") == std::string::npos,
               "rewrite removes superseded SET command");
        expect(text.find("removed") == std::string::npos,
               "rewrite omits deleted key");
    }

    {
        miniredis::KeyValueStore recovered;
        miniredis::AofPersistence persistence(path.string());
        std::string error;
        expect(persistence.replay(recovered, error), "AOF replay succeeds: " + error);
        expect(recovered.get("name") == std::optional<std::string>("updated"),
               "restarted store recovers latest value");
        expect(!recovered.exists("removed"), "deleted key stays deleted after replay");
        expect(recovered.ttl("name") >= 0, "replayed expiration is restored");
    }

    expect(!std::filesystem::exists(temporary_path), "AOF temporary file is cleaned up");
    std::filesystem::remove(path, ignored);
    std::cout << "PASS: AOF integration tests (rewrite, close, replay)\n";
    return EXIT_SUCCESS;
}
