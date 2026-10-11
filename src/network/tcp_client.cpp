#include "miniredis/tcp_client.hpp"

#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
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

}  // namespace

bool TcpClient::connectTo(const std::string& host, std::uint16_t port, std::string& error) {
    close();
    Socket socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (!socket.valid()) {
        error = "socket creation failed: " + lastSocketError();
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
#ifdef _WIN32
    const auto address_value = ::inet_addr(host.c_str());
    if (address_value == INADDR_NONE) {
        error = "host must be an IPv4 address: " + host;
        return false;
    }
    address.sin_addr.s_addr = address_value;
#else
    if (::inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        error = "host must be an IPv4 address: " + host;
        return false;
    }
#endif
    if (::connect(socket.nativeHandle(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        error = "connect failed: " + lastSocketError();
        return false;
    }
    socket_ = std::move(socket);
    return true;
}

bool TcpClient::sendCommand(const std::string& command, std::string& reply, std::string& error,
                            std::size_t max_reply_length) {
    if (!connected()) {
        error = "client is not connected";
        return false;
    }
    if (!socket_.sendAll(command + "\n", error)) {
        return false;
    }
    return socket_.receiveLine(reply, max_reply_length, error);
}

}  // namespace miniredis
