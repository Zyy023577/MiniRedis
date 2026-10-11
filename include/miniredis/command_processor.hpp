#pragma once

#include "miniredis/key_value_store.hpp"

#include <sstream>
#include <string>

namespace miniredis {

class AofPersistence;
class ServerMetrics;

struct CommandReply {
    bool success{false};
    bool should_close{false};
    std::string text;
};

class CommandProcessor {
public:
    explicit CommandProcessor(KeyValueStore& store, AofPersistence* persistence = nullptr,
                              ServerMetrics* metrics = nullptr);

    CommandReply execute(const std::string& line);

private:
    static std::string trim(const std::string& text);
    static std::string upper(std::string text);
    static CommandReply error(const std::string& message);
    static bool hasExtraArgument(std::istringstream& input);

    KeyValueStore& store_;
    AofPersistence* persistence_{nullptr};
    ServerMetrics* metrics_{nullptr};
};

}  // namespace miniredis
