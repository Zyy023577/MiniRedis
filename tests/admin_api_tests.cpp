#include "miniredis/admin_api.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

namespace {

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

std::string request(const std::string& method, const std::string& path, const std::string& body = {}) {
    miniredis::Socket socket(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    expect(socket.valid(), "HTTP test socket created");

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(18081);
#ifdef _WIN32
    address.sin_addr.s_addr = ::inet_addr("127.0.0.1");
#else
    expect(::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr) == 1, "HTTP test address parsed");
#endif
    expect(::connect(socket.nativeHandle(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
           "HTTP test client connected");

    const std::string raw = method + " " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\n" +
                            "Content-Type: application/json\r\nConnection: close\r\nContent-Length: " +
                            std::to_string(body.size()) + "\r\n\r\n" + body;
    std::string error;
    expect(socket.sendAll(raw, error), "HTTP test request sent: " + error);

    std::string response;
    char buffer[2048];
    while (true) {
        const int count = socket.receive(buffer, sizeof(buffer), error);
        if (count <= 0) break;
        response.append(buffer, static_cast<std::size_t>(count));
    }
    return response;
}

}  // namespace

int main() {
    miniredis::SocketRuntime runtime;
    const auto path = std::filesystem::temp_directory_path() / "miniredis_admin_api_test.aof";
    const auto static_root = std::filesystem::temp_directory_path() / "miniredis_admin_static_test";
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::filesystem::remove_all(static_root, ignored);
    std::filesystem::create_directories(static_root / "assets");
    std::ofstream(static_root / "index.html") << "<html>MiniRedis Dashboard</html>";
    std::ofstream(static_root / "assets" / "app.css") << "body { color: white; }";

    miniredis::KeyValueStore store;
    miniredis::AofPersistence persistence(path.string());
    miniredis::ServerMetrics metrics;
    miniredis::AdminApiServer api(store, &persistence, &metrics, "127.0.0.1", 18081,
                                  65536, static_root.string());
    std::string server_error;
    int server_result = -1;
    std::thread server_thread([&] { server_result = api.run(server_error); });
    std::this_thread::sleep_for(std::chrono::milliseconds(150));

    const auto created = request("POST", "/api/keys", R"({"key":"api-key","value":"ayu","ttl":60})");
    expect(created.find("200 OK") != std::string::npos && created.find("success") != std::string::npos,
           "API creates key");
    const auto fetched = request("GET", "/api/keys/api-key");
    expect(fetched.find("200 OK") != std::string::npos && fetched.find("ayu") != std::string::npos,
           "API reads key");
    const auto updated = request("PUT", "/api/keys/api-key", R"({"value":"updated"})");
    expect(updated.find("200 OK") != std::string::npos, "API updates key");
    const auto invalid = request("POST", "/api/keys", R"({"key":"bad","value":"value","ttl":-1})");
    expect(invalid.find("400 Bad Request") != std::string::npos, "API rejects invalid TTL");
    const auto command = request("POST", "/api/command", R"({"command":"GET api-key"})");
    expect(command.find("updated") != std::string::npos, "API executes command");
    const auto stats = request("GET", "/api/stats");
    expect(stats.find("total_commands") != std::string::npos && stats.find("get_hits") != std::string::npos,
           "API returns stats");
    const auto rewrite = request("POST", "/api/aof/rewrite");
    expect(rewrite.find("200 OK") != std::string::npos, "API rewrites AOF");
    const auto deleted = request("DELETE", "/api/keys/api-key");
    expect(deleted.find("200 OK") != std::string::npos, "API deletes key");
    const auto missing = request("GET", "/api/keys/api-key");
    expect(missing.find("404 Not Found") != std::string::npos, "API reports missing key");
    const auto page = request("GET", "/");
    expect(page.find("200 OK") != std::string::npos &&
               page.find("MiniRedis Dashboard") != std::string::npos,
           "API serves dashboard index");
    const auto asset = request("GET", "/assets/app.css");
    expect(asset.find("200 OK") != std::string::npos && asset.find("color: white") != std::string::npos,
           "API serves dashboard asset");
    const auto traversal = request("GET", "/../index.html");
    expect(traversal.find("404 Not Found") != std::string::npos,
           "API rejects static path traversal");

    api.stop();
    server_thread.join();
    expect(server_result == 0, "Admin API stops cleanly: " + server_error);
    std::filesystem::remove(path, ignored);
    std::filesystem::remove(path.string() + ".tmp", ignored);
    std::filesystem::remove_all(static_root, ignored);
    std::cout << "PASS: Admin API integration tests (CRUD, command, stats, rewrite)\n";
    return EXIT_SUCCESS;
}
