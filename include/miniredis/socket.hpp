#pragma once

#include <cstddef>
#include <string>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#endif

namespace miniredis {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket kInvalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket kInvalidSocket = -1;
#endif

class SocketRuntime {
public:
    SocketRuntime();
    ~SocketRuntime();

    SocketRuntime(const SocketRuntime&) = delete;
    SocketRuntime& operator=(const SocketRuntime&) = delete;
};

class Socket {
public:
    Socket() = default;
    explicit Socket(NativeSocket handle) : handle_(handle) {}
    ~Socket();

    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    Socket(Socket&& other) noexcept;
    Socket& operator=(Socket&& other) noexcept;

    bool valid() const { return handle_ != kInvalidSocket; }
    NativeSocket nativeHandle() const { return handle_; }
    void close();

    bool sendAll(const std::string& data, std::string& error) const;
    int receive(char* buffer, std::size_t capacity, std::string& error) const;
    bool receiveLine(std::string& line, std::size_t max_length, std::string& error) const;

private:
    NativeSocket handle_{kInvalidSocket};
};

}  // namespace miniredis
