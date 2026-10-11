#include "miniredis/tcp_client.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

namespace {
std::uint16_t parsePort(const char* text) {
    const auto value = std::strtoul(text, nullptr, 10);
    return value <= 65535 ? static_cast<std::uint16_t>(value) : 0;
}
}  // namespace

int main(int argc, char* argv[]) {
    const std::string host = argc > 1 ? argv[1] : "127.0.0.1";
    const std::uint16_t port = argc > 2 ? parsePort(argv[2]) : 6380;
    if (port == 0) {
        std::cerr << "Usage: miniredis_cli [ipv4-host] [port]\n";
        return EXIT_FAILURE;
    }

    try {
        miniredis::SocketRuntime runtime;
        miniredis::TcpClient client;
        std::string error;
        if (!client.connectTo(host, port, error)) {
            std::cerr << error << '\n';
            return EXIT_FAILURE;
        }

        std::cout << "Connected to MiniRedis. Enter a command, or QUIT to exit.\n";
        std::string command;
        while (std::getline(std::cin, command)) {
            std::string reply;
            if (!client.sendCommand(command, reply, error)) {
                std::cerr << error << '\n';
                return EXIT_FAILURE;
            }
            std::cout << reply << '\n';
            if (command == "QUIT" || command == "quit") {
                break;
            }
        }
        client.close();
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
