#pragma once

#include "miniredis/key_value_store.hpp"

#include <fstream>
#include <mutex>
#include <string>

namespace miniredis {

class AofPersistence {
public:
    explicit AofPersistence(std::string path);
    ~AofPersistence();

    AofPersistence(const AofPersistence&) = delete;
    AofPersistence& operator=(const AofPersistence&) = delete;

    bool replay(KeyValueStore& store, std::string& error);
    bool appendCommand(const std::string& command, std::string& error);
    bool rewrite(KeyValueStore& store, std::string& error);
    const std::string& path() const { return path_; }

private:
    bool openForAppend(std::string& error);
    bool replaceFile(const std::string& temporary_path, std::string& error);

    const std::string path_;
    std::ofstream file_;
    std::mutex mutex_;
};

}  // namespace miniredis
