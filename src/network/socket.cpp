#include "miniredis/socket.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <netinet/in.h>
#include <unistd.h>
#endif

namespace miniredis {

namespace {

std::string lastSocketError() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

void closeNativeSocket(NativeSocket handle) {
    if (handle == kInvalidSocket) {
        return;
    }
#ifdef _WIN32
    ::closesocket(handle);
#else
    ::close(handle);
#endif
}

}  // namespace

SocketRuntime::SocketRuntime() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif
}

SocketRuntime::~SocketRuntime() {
#ifdef _WIN32
    WSACleanup();
#endif
}

Socket::~Socket() {
    close();
}

Socket::Socket(Socket&& other) noexcept : handle_(other.handle_) {
    other.handle_ = kInvalidSocket;
}

Socket& Socket::operator=(Socket&& other) noexcept {
    if (this != &other) {
        close();
        handle_ = other.handle_;
        other.handle_ = kInvalidSocket;
    }
    return *this;
}

void Socket::close() {
    closeNativeSocket(handle_);
    handle_ = kInvalidSocket;
}

bool Socket::sendAll(const std::string& data, std::string& error) const {
    std::size_t sent = 0;
    while (sent < data.size()) {
#ifdef _WIN32
        const int count = ::send(handle_, data.data() + sent,
                                 static_cast<int>(data.size() - sent), 0);
#else
        const auto count = ::send(handle_, data.data() + sent, data.size() - sent, 0);
#endif
        if (count <= 0) {
            error = lastSocketError();
            return false;
        }
        sent += static_cast<std::size_t>(count);
    }
    return true;
}

int Socket::receive(char* buffer, std::size_t capacity, std::string& error) const {
#ifdef _WIN32
    const int count = ::recv(handle_, buffer, static_cast<int>(capacity), 0);
#else
    const auto count = ::recv(handle_, buffer, capacity, 0);
#endif
    if (count < 0) {
        error = lastSocketError();
    }
    return count;
}

bool Socket::receiveLine(std::string& line, std::size_t max_length, std::string& error) const {
    line.clear();
    char character = '\0';
    while (line.size() <= max_length) {
        const int count = receive(&character, 1, error);
        if (count <= 0) {
            if (count == 0) {
                error = "connection closed";
            }
            return false;
        }
        if (character == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return true;
        }
        line.push_back(character);
    }
    error = "request line is too long";
    return false;
}

}  // namespace miniredis
