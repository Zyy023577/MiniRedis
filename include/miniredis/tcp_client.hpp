#pragma once

#include "miniredis/socket.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace miniredis {

class TcpClient {
public:
    bool connectTo(const std::string& host, std::uint16_t port, std::string& error);
    bool sendCommand(const std::string& command, std::string& reply, std::string& error,
                     std::size_t max_reply_length = 8192);
    void close() { socket_.close(); }
    bool connected() const { return socket_.valid(); }

private:
    Socket socket_;
};

}  // namespace miniredis
