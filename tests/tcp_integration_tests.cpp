#include "miniredis/tcp_client.hpp"
#include "miniredis/tcp_server.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

bool command(miniredis::TcpClient& client, const std::string& text, const std::string& expected,
             std::string& error) {
    std::string reply;
    if (!client.sendCommand(text, reply, error)) {
        return false;
    }
    expect(reply == expected, text + " reply was '" + reply + "'");
    return true;
}

}  // namespace

int main() {
    miniredis::KeyValueStore store;
    miniredis::TcpServer server(store, "127.0.0.1", 16380);
    std::string server_error;
    int server_result = -1;
    std::thread server_thread([&] { server_result = server.run(server_error); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    miniredis::SocketRuntime runtime;
    miniredis::TcpClient first;
    miniredis::TcpClient second;
    std::string error;
    expect(first.connectTo("127.0.0.1", 16380, error), "first client connects: " + error);
    expect(second.connectTo("127.0.0.1", 16380, error), "second client connects: " + error);
    expect(command(first, "SET shared first", "OK", error), "first client SET");
    expect(command(second, "GET shared", "first", error), "second client GET");
    expect(command(first, "UNKNOWN", "(error) ERR unknown command 'UNKNOWN'", error),
           "invalid command returns an error");
    expect(command(first, "SET still-alive yes", "OK", error),
           "server remains alive after invalid command");

    const std::string long_command = "SET long " + std::string(9000, 'x');
    std::string long_reply;
    expect(first.sendCommand(long_command, long_reply, error), "long request receives a reply");
    expect(long_reply == "(error) ERR request is too long", "long request is rejected");

    expect(command(second, "QUIT", "OK", error), "QUIT returns OK");
    first.close();
    second.close();
    server.stop();
    server_thread.join();
    expect(server_result == 0, "server stops without error: " + server_error);

    std::cout << "PASS: TCP integration tests (2 clients, invalid input, long request)\n";
    return EXIT_SUCCESS;
}
