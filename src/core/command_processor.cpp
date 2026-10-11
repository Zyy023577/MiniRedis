#include "miniredis/command_processor.hpp"

#include "miniredis/aof_persistence.hpp"
#include "miniredis/metrics.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace miniredis {

CommandProcessor::CommandProcessor(KeyValueStore& store, AofPersistence* persistence,
                                   ServerMetrics* metrics)
    : store_(store), persistence_(persistence), metrics_(metrics) {}

std::string CommandProcessor::trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string CommandProcessor::upper(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    return text;
}

CommandReply CommandProcessor::error(const std::string& message) {
    return CommandReply{false, false, "(error) " + message};
}

bool CommandProcessor::hasExtraArgument(std::istringstream& input) {
    std::string extra;
    return static_cast<bool>(input >> extra);
}

CommandReply CommandProcessor::execute(const std::string& line) {
    const std::string cleaned = trim(line);
    if (cleaned.empty()) {
        return error("ERR empty command");
    }

    std::istringstream input(cleaned);
    std::string command;
    input >> command;
    command = upper(command);
    if (metrics_ != nullptr) {
        metrics_->recordCommand(command);
    }

    if (command == "SET") {
        std::string key;
        if (!(input >> key)) {
            return error("ERR SET requires key and value");
        }
        std::string value;
        std::getline(input, value);
        value = trim(value);
        if (value.empty()) {
            return error("ERR SET requires key and value");
        }

        std::string message;
        if (!store_.set(key, value, message)) {
            return error("ERR " + message);
        }
        if (persistence_ != nullptr &&
            !persistence_->appendCommand("SET " + key + " " + value, message)) {
            return error("ERR persistence write failed: " + message);
        }
        return CommandReply{true, false, "OK"};
    }

    if (command == "GET") {
        std::string key;
        if (!(input >> key) || hasExtraArgument(input)) {
            return error("ERR GET requires exactly one key");
        }
        const auto value = store_.get(key);
        if (metrics_ != nullptr) {
            metrics_->recordGetResult(value.has_value());
        }
        return CommandReply{true, false, value.has_value() ? *value : "(nil)"};
    }

    if (command == "DEL") {
        std::string key;
        if (!(input >> key) || hasExtraArgument(input)) {
            return error("ERR DEL requires exactly one key");
        }
        const bool removed = store_.del(key);
        if (removed && persistence_ != nullptr) {
            std::string message;
            if (!persistence_->appendCommand("DEL " + key, message)) {
                return error("ERR persistence write failed: " + message);
            }
        }
        return CommandReply{true, false, "(integer) " + std::to_string(removed ? 1 : 0)};
    }

    if (command == "EXISTS") {
        std::string key;
        if (!(input >> key) || hasExtraArgument(input)) {
            return error("ERR EXISTS requires exactly one key");
        }
        return CommandReply{true, false, "(integer) " + std::to_string(store_.exists(key) ? 1 : 0)};
    }

    if (command == "KEYS") {
        if (hasExtraArgument(input)) {
            return error("ERR KEYS takes no arguments");
        }
        const auto all_keys = store_.keys();
        if (all_keys.empty()) {
            return CommandReply{true, false, "(empty list)"};
        }
        std::ostringstream output;
        for (std::size_t index = 0; index < all_keys.size(); ++index) {
            if (index != 0) {
                output << ' ';
            }
            output << all_keys[index];
        }
        return CommandReply{true, false, output.str()};
    }

    if (command == "EXPIRE") {
        std::string key;
        std::int64_t seconds = 0;
        if (!(input >> key >> seconds) || hasExtraArgument(input)) {
            return error("ERR EXPIRE requires key and seconds");
        }
        std::string message;
        if (!store_.expire(key, seconds, message)) {
            return error("ERR " + message);
        }
        if (persistence_ != nullptr &&
            !persistence_->appendCommand("EXPIRE " + key + " " + std::to_string(seconds), message)) {
            return error("ERR persistence write failed: " + message);
        }
        return CommandReply{true, false, "OK"};
    }

    if (command == "TTL") {
        std::string key;
        if (!(input >> key) || hasExtraArgument(input)) {
            return error("ERR TTL requires exactly one key");
        }
        return CommandReply{true, false, "(integer) " + std::to_string(store_.ttl(key))};
    }

    if (command == "INFO") {
        if (hasExtraArgument(input)) {
            return error("ERR INFO takes no arguments");
        }
        const auto store_info = store_.info();
        return CommandReply{true, false,
                            "keys=" + std::to_string(store_info.key_count) +
                                " max_key_length=" + std::to_string(store_info.max_key_length) +
                                " max_value_length=" + std::to_string(store_info.max_value_length) +
                                " max_key_count=" + std::to_string(store_info.max_key_count) +
                                " estimated_memory_bytes=" +
                                std::to_string(store_info.estimated_memory_bytes)};
    }

    if (command == "AOF") {
        std::string subcommand;
        if (!(input >> subcommand) || upper(subcommand) != "REWRITE" || hasExtraArgument(input)) {
            return error("ERR use AOF REWRITE");
        }
        if (persistence_ == nullptr) {
            return error("ERR AOF persistence is disabled");
        }
        std::string message;
        if (!persistence_->rewrite(store_, message)) {
            return error("ERR AOF rewrite failed: " + message);
        }
        return CommandReply{true, false, "OK"};
    }

    if (command == "QUIT") {
        if (hasExtraArgument(input)) {
            return error("ERR QUIT takes no arguments");
        }
        return CommandReply{true, true, "OK"};
    }

    return error("ERR unknown command '" + command + "'");
}

}  // namespace miniredis
