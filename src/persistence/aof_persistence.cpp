#include "miniredis/aof_persistence.hpp"

#include "miniredis/command_processor.hpp"

#include <filesystem>
#include <sstream>
#include <utility>

#ifdef _WIN32
#include <windows.h>
#endif

namespace miniredis {

namespace {

bool hasLineBreak(const std::string& text) {
    return text.find('\n') != std::string::npos || text.find('\r') != std::string::npos;
}

}  // namespace

AofPersistence::AofPersistence(std::string path) : path_(std::move(path)) {}

AofPersistence::~AofPersistence() {
    std::lock_guard lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
}

bool AofPersistence::openForAppend(std::string& error) {
    if (file_.is_open()) {
        return true;
    }
    const std::filesystem::path file_path(path_);
    if (file_path.has_parent_path()) {
        std::error_code directory_error;
        std::filesystem::create_directories(file_path.parent_path(), directory_error);
        if (directory_error) {
            error = "cannot create AOF directory: " + directory_error.message();
            return false;
        }
    }
    file_.open(file_path, std::ios::out | std::ios::app);
    if (!file_.is_open()) {
        error = "cannot open AOF file: " + path_;
        return false;
    }
    return true;
}

bool AofPersistence::appendCommand(const std::string& command, std::string& error) {
    if (command.empty() || hasLineBreak(command)) {
        error = "AOF command must be a single non-empty line";
        return false;
    }

    std::lock_guard lock(mutex_);
    if (!openForAppend(error)) {
        return false;
    }
    file_ << command << '\n';
    file_.flush();
    if (!file_) {
        error = "failed to write AOF file: " + path_;
        file_.clear();
        return false;
    }
    return true;
}

bool AofPersistence::replay(KeyValueStore& store, std::string& error) {
    std::lock_guard lock(mutex_);
    std::ifstream input(path_);
    if (!input.is_open()) {
        std::error_code status_error;
        const bool exists = std::filesystem::exists(path_, status_error);
        if (!exists && !status_error) {
            return openForAppend(error);
        }
        error = "cannot read AOF file: " + path_;
        return false;
    }

    CommandProcessor processor(store);
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        if (line.empty()) {
            continue;
        }
        const auto reply = processor.execute(line);
        if (!reply.success) {
            error = "AOF replay failed at line " + std::to_string(line_number) + ": " + reply.text;
            return false;
        }
    }
    if (input.bad()) {
        error = "failed while reading AOF file: " + path_;
        return false;
    }
    input.close();
    return openForAppend(error);
}

bool AofPersistence::replaceFile(const std::string& temporary_path, std::string& error) {
#ifdef _WIN32
    const std::wstring source = std::filesystem::path(temporary_path).wstring();
    const std::wstring target = std::filesystem::path(path_).wstring();
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error = "cannot atomically replace AOF file: " + std::to_string(GetLastError());
        return false;
    }
    return true;
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary_path, path_, rename_error);
    if (rename_error) {
        error = "cannot atomically replace AOF file: " + rename_error.message();
        return false;
    }
    return true;
#endif
}

bool AofPersistence::rewrite(KeyValueStore& store, std::string& error) {
    const std::string temporary_path = path_ + ".tmp";

    std::lock_guard lock(mutex_);
    const auto snapshot = store.snapshot();
    const std::filesystem::path file_path(path_);
    if (file_path.has_parent_path()) {
        std::error_code directory_error;
        std::filesystem::create_directories(file_path.parent_path(), directory_error);
        if (directory_error) {
            error = "cannot create AOF directory: " + directory_error.message();
            return false;
        }
    }

    std::ofstream temporary(temporary_path, std::ios::out | std::ios::trunc);
    if (!temporary.is_open()) {
        error = "cannot open AOF temporary file: " + temporary_path;
        return false;
    }
    for (const auto& entry : snapshot) {
        if (hasLineBreak(entry.key) || hasLineBreak(entry.value)) {
            error = "cannot rewrite AOF: key or value contains a line break";
            temporary.close();
            std::error_code ignored;
            std::filesystem::remove(temporary_path, ignored);
            return false;
        }
        temporary << "SET " << entry.key << ' ' << entry.value << '\n';
        if (entry.ttl_seconds.has_value() && *entry.ttl_seconds > 0) {
            temporary << "EXPIRE " << entry.key << ' ' << *entry.ttl_seconds << '\n';
        }
    }
    temporary.flush();
    if (!temporary) {
        error = "failed to write AOF temporary file: " + temporary_path;
        temporary.close();
        return false;
    }
    temporary.close();

    if (file_.is_open()) {
        file_.flush();
        file_.close();
    }
    if (!replaceFile(temporary_path, error)) {
        return false;
    }
    return openForAppend(error);
}

}  // namespace miniredis
