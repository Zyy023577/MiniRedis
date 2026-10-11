#include "miniredis/admin_api.hpp"

#include "miniredis/command_processor.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace miniredis {

namespace {

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

std::string socketError() {
#ifdef _WIN32
    return "socket error " + std::to_string(WSAGetLastError());
#else
    return std::strerror(errno);
#endif
}

void closeNativeSocket(NativeSocket handle) {
#ifdef _WIN32
    ::closesocket(handle);
#else
    ::close(handle);
#endif
}

bool setReuseAddress(NativeSocket socket, std::string& error) {
    int enabled = 1;
#ifdef _WIN32
    const int result = ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
                                    reinterpret_cast<const char*>(&enabled), sizeof(enabled));
#else
    const int result = ::setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#endif
    if (result != 0) {
        error = "setsockopt(SO_REUSEADDR) failed: " + socketError();
        return false;
    }
    return true;
}

std::string trim(std::string text) {
    while (!text.empty() && (text.back() == '\r' || text.back() == '\n' || text.back() == ' ' || text.back() == '\t')) {
        text.pop_back();
    }
    std::size_t first = 0;
    while (first < text.size() && (text[first] == ' ' || text[first] == '\t')) {
        ++first;
    }
    return text.substr(first);
}

int hexValue(char value) {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

std::string urlDecode(const std::string& text) {
    std::string result;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '%' && index + 2 < text.size()) {
            const int high = hexValue(text[index + 1]);
            const int low = hexValue(text[index + 2]);
            if (high >= 0 && low >= 0) {
                result.push_back(static_cast<char>(high * 16 + low));
                index += 2;
                continue;
            }
        }
        result.push_back(text[index] == '+' ? ' ' : text[index]);
    }
    return result;
}

std::string jsonEscape(const std::string& text) {
    std::ostringstream output;
    for (const unsigned char value : text) {
        switch (value) {
            case '"': output << "\\\""; break;
            case '\\': output << "\\\\"; break;
            case '\n': output << "\\n"; break;
            case '\r': output << "\\r"; break;
            case '\t': output << "\\t"; break;
            default:
                if (value < 0x20) {
                    output << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                           << static_cast<int>(value) << std::dec;
                } else {
                    output << value;
                }
        }
    }
    return output.str();
}

bool jsonString(const std::string& body, const std::string& field, std::string& value) {
    const std::string marker = "\"" + field + "\"";
    const auto field_position = body.find(marker);
    if (field_position == std::string::npos) return false;
    const auto colon = body.find(':', field_position + marker.size());
    const auto quote = body.find('"', colon == std::string::npos ? field_position : colon);
    if (colon == std::string::npos || quote == std::string::npos) return false;

    value.clear();
    bool escaped = false;
    for (std::size_t index = quote + 1; index < body.size(); ++index) {
        const char character = body[index];
        if (escaped) {
            switch (character) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                default: value.push_back(character); break;
            }
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return true;
        } else {
            value.push_back(character);
        }
    }
    return false;
}

bool jsonHasField(const std::string& body, const std::string& field) {
    return body.find("\"" + field + "\"") != std::string::npos;
}

bool jsonInteger(const std::string& body, const std::string& field, std::int64_t& value) {
    const std::string marker = "\"" + field + "\"";
    const auto field_position = body.find(marker);
    if (field_position == std::string::npos) return false;
    const auto colon = body.find(':', field_position + marker.size());
    if (colon == std::string::npos) return false;
    std::size_t start = colon + 1;
    while (start < body.size() && std::isspace(static_cast<unsigned char>(body[start]))) ++start;
    try {
        std::size_t consumed = 0;
        value = std::stoll(body.substr(start), &consumed);
        return consumed > 0;
    } catch (...) {
        return false;
    }
}

bool readRequest(Socket& client, std::size_t max_length, HttpRequest& request, std::string& error) {
    std::string raw;
    char buffer[2048];
    std::size_t content_length = 0;
    while (raw.find("\r\n\r\n") == std::string::npos) {
        const int count = client.receive(buffer, sizeof(buffer), error);
        if (count <= 0) {
            if (count == 0) error = "connection closed";
            return false;
        }
        raw.append(buffer, static_cast<std::size_t>(count));
        if (raw.size() > max_length) {
            error = "HTTP request is too long";
            return false;
        }
    }

    const auto header_end = raw.find("\r\n\r\n");
    const std::string header = raw.substr(0, header_end);
    std::istringstream lines(header);
    std::string request_line;
    if (!std::getline(lines, request_line)) {
        error = "invalid HTTP request line";
        return false;
    }
    request_line = trim(request_line);
    std::istringstream request_parts(request_line);
    if (!(request_parts >> request.method >> request.path)) {
        error = "invalid HTTP request line";
        return false;
    }

    std::string header_line;
    while (std::getline(lines, header_line)) {
        header_line = trim(header_line);
        const auto separator = header_line.find(':');
        if (separator == std::string::npos) continue;
        std::string name = header_line.substr(0, separator);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (name == "content-length") {
            try {
                content_length = static_cast<std::size_t>(std::stoull(trim(header_line.substr(separator + 1))));
            } catch (...) {
                error = "invalid Content-Length";
                return false;
            }
        }
    }
    if (content_length > max_length || header_end + 4 + content_length > max_length) {
        error = "HTTP request body is too long";
        return false;
    }
    while (raw.size() < header_end + 4 + content_length) {
        const int count = client.receive(buffer, sizeof(buffer), error);
        if (count <= 0) return false;
        raw.append(buffer, static_cast<std::size_t>(count));
        if (raw.size() > max_length) {
            error = "HTTP request is too long";
            return false;
        }
    }
    request.body = raw.substr(header_end + 4, content_length);
    return true;
}

std::string httpResponse(int status, const std::string& status_text, const std::string& body,
                         const std::string& content_type = "application/json; charset=utf-8") {
    std::ostringstream response;
    response << "HTTP/1.1 " << status << ' ' << status_text << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "Access-Control-Allow-Origin: *\r\n"
             << "Access-Control-Allow-Methods: GET, POST, PUT, DELETE, OPTIONS\r\n"
             << "Access-Control-Allow-Headers: Content-Type\r\n\r\n"
             << body;
    return response.str();
}

std::string errorJson(const std::string& message) {
    return "{\"error\":\"" + jsonEscape(message) + "\"}";
}

std::string contentTypeFor(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".html") return "text/html; charset=utf-8";
    if (extension == ".css") return "text/css; charset=utf-8";
    if (extension == ".js") return "text/javascript; charset=utf-8";
    if (extension == ".json") return "application/json; charset=utf-8";
    if (extension == ".svg") return "image/svg+xml";
    if (extension == ".png") return "image/png";
    if (extension == ".jpg" || extension == ".jpeg") return "image/jpeg";
    if (extension == ".ico") return "image/x-icon";
    return "application/octet-stream";
}

bool readStaticFile(const std::string& root, const std::string& request_path,
                    std::string& body, std::string& content_type) {
    if (root.empty()) return false;

    std::string relative_text = request_path == "/" ? "index.html" : request_path.substr(1);
    relative_text = urlDecode(relative_text);
    const std::filesystem::path relative(relative_text);
    if (relative.is_absolute() || relative.has_root_name() || relative.has_root_directory()) {
        return false;
    }
    for (const auto& part : relative) {
        if (part == ".." || part == ".") return false;
    }

    const std::filesystem::path root_path(root);
    std::filesystem::path candidate = root_path / relative;
    std::ifstream file(candidate, std::ios::binary);
    if (!file && relative.extension().empty()) {
        candidate = root_path / "index.html";
        file.open(candidate, std::ios::binary);
    }
    if (!file) return false;

    body.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    content_type = contentTypeFor(candidate);
    return true;
}

}  // namespace

AdminApiServer::AdminApiServer(KeyValueStore& store, AofPersistence* persistence,
                               ServerMetrics* metrics, std::string host, std::uint16_t port,
                               std::size_t max_request_length, std::string static_root)
    : store_(store),
      persistence_(persistence),
      metrics_(metrics),
      host_(std::move(host)),
      port_(port),
      max_request_length_(max_request_length),
      static_root_(std::move(static_root)) {}

AdminApiServer::~AdminApiServer() {
    stop();
    closeActiveClients();
    for (auto& thread : client_threads_) {
        if (thread.joinable()) thread.join();
    }
}

void AdminApiServer::stop() {
    stop_requested_.store(true);
}

void AdminApiServer::registerClient(NativeSocket client_socket) {
    std::lock_guard lock(clients_mutex_);
    active_sockets_.push_back(client_socket);
}

void AdminApiServer::unregisterClient(NativeSocket client_socket) {
    std::lock_guard lock(clients_mutex_);
    const auto it = std::find(active_sockets_.begin(), active_sockets_.end(), client_socket);
    if (it != active_sockets_.end()) active_sockets_.erase(it);
}

void AdminApiServer::closeActiveClients() {
    std::lock_guard lock(clients_mutex_);
    for (const auto socket : active_sockets_) closeNativeSocket(socket);
}

void AdminApiServer::handleClient(NativeSocket client_socket) {
    registerClient(client_socket);
    Socket client(client_socket);
    HttpRequest request;
    std::string error;
    std::string response;

    if (!readRequest(client, max_request_length_, request, error)) {
        response = httpResponse(400, "Bad Request", errorJson(error));
    } else if (request.method == "OPTIONS") {
        response = httpResponse(204, "No Content", "");
    } else {
        const auto query_path = request.path.substr(0, request.path.find('?'));
        CommandProcessor commands(store_, persistence_, metrics_);
        int status = 200;
        std::string body;
        std::string response_content_type = "application/json; charset=utf-8";

        if (request.method == "GET" && query_path == "/api/keys") {
            auto entries = store_.snapshot();
            std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
                return left.key < right.key;
            });
            std::ostringstream json;
            json << "{\"keys\":[";
            for (std::size_t index = 0; index < entries.size(); ++index) {
                if (index != 0) json << ',';
                json << "{\"key\":\"" << jsonEscape(entries[index].key) << "\",\"ttl\":"
                     << (entries[index].ttl_seconds.has_value() ? *entries[index].ttl_seconds : -1) << '}';
            }
            json << "]}";
            body = json.str();
        } else if (request.method == "GET" && query_path.rfind("/api/keys/", 0) == 0) {
            const std::string key = urlDecode(query_path.substr(std::string("/api/keys/").size()));
            const auto value = store_.get(key);
            if (!value.has_value()) {
                status = 404;
                body = errorJson("key does not exist");
            } else {
                body = "{\"key\":\"" + jsonEscape(key) + "\",\"value\":\"" +
                       jsonEscape(*value) + "\",\"ttl\":" + std::to_string(store_.ttl(key)) + "}";
            }
        } else if ((request.method == "POST" && query_path == "/api/keys") ||
                   (request.method == "PUT" && query_path.rfind("/api/keys/", 0) == 0)) {
            std::string key = request.method == "PUT"
                                  ? urlDecode(query_path.substr(std::string("/api/keys/").size()))
                                  : std::string{};
            std::string value;
            std::int64_t ttl = -1;
            const bool has_key = request.method == "PUT" || jsonString(request.body, "key", key);
            if (!has_key || key.empty() || !jsonString(request.body, "value", value)) {
                status = 400;
                body = errorJson("body must contain a valid key and string field value");
            } else {
                auto reply = commands.execute("SET " + key + " " + value);
                if (!reply.success) {
                    status = 400;
                    body = errorJson(reply.text);
                } else if (jsonHasField(request.body, "ttl")) {
                    if (!jsonInteger(request.body, "ttl", ttl) || ttl < 0) {
                        status = 400;
                        body = errorJson("ttl must be a non-negative integer");
                    } else {
                        reply = commands.execute("EXPIRE " + key + " " + std::to_string(ttl));
                        if (!reply.success) {
                            status = 400;
                            body = errorJson(reply.text);
                        }
                    }
                }
                if (body.empty()) body = "{\"success\":true}";
            }
        } else if (request.method == "DELETE" && query_path.rfind("/api/keys/", 0) == 0) {
            const std::string key = urlDecode(query_path.substr(std::string("/api/keys/").size()));
            const auto reply = commands.execute("DEL " + key);
            if (!reply.success) {
                status = 400;
                body = errorJson(reply.text);
            } else if (reply.text == "(integer) 0") {
                status = 404;
                body = errorJson("key does not exist");
            } else {
                body = "{\"success\":true}";
            }
        } else if (request.method == "POST" && query_path == "/api/command") {
            std::string command;
            if (!jsonString(request.body, "command", command)) {
                status = 400;
                body = errorJson("body must contain string field command");
            } else {
                const auto reply = commands.execute(command);
                body = "{\"success\":" + std::string(reply.success ? "true" : "false") +
                       ",\"reply\":\"" + jsonEscape(reply.text) + "\"}";
                if (!reply.success) status = 400;
            }
        } else if (request.method == "GET" && query_path == "/api/stats") {
            const auto info = store_.info();
            const auto metrics = metrics_ != nullptr ? metrics_->snapshot() : MetricsSnapshot{};
            const double hit_rate = metrics.get_commands == 0
                                        ? 0.0
                                        : static_cast<double>(metrics.get_hits) /
                                              static_cast<double>(metrics.get_commands);
            std::ostringstream json;
            json << "{\"key_count\":" << info.key_count
                 << ",\"active_clients\":" << metrics.active_clients
                 << ",\"total_commands\":" << metrics.total_commands
                 << ",\"get_commands\":" << metrics.get_commands
                 << ",\"set_commands\":" << metrics.set_commands
                 << ",\"get_hits\":" << metrics.get_hits
                 << ",\"hit_rate\":" << hit_rate
                 << ",\"estimated_memory_bytes\":" << info.estimated_memory_bytes << '}';
            body = json.str();
        } else if (request.method == "POST" && query_path == "/api/aof/rewrite") {
            if (persistence_ == nullptr) {
                status = 503;
                body = errorJson("AOF persistence is disabled");
            } else {
                std::string rewrite_error;
                if (!persistence_->rewrite(store_, rewrite_error)) {
                    status = 500;
                    body = errorJson(rewrite_error);
                } else {
                    body = "{\"success\":true}";
                }
            }
        } else if (request.method == "GET" && !static_root_.empty()) {
            if (!readStaticFile(static_root_, query_path, body, response_content_type)) {
                status = 404;
                body = errorJson("static file not found");
            }
        } else {
            status = 404;
            body = errorJson("endpoint not found");
        }
        const std::string status_text = status == 200 ? "OK" : (status == 404 ? "Not Found" :
                                          (status == 400 ? "Bad Request" : "Internal Server Error"));
        response = httpResponse(status, status_text, body, response_content_type);
    }

    std::string send_error;
    client.sendAll(response, send_error);
    client.close();
    unregisterClient(client_socket);
}

int AdminApiServer::run(std::string& error) {
    try {
        SocketRuntime runtime;
        Socket listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!listener.valid()) {
            error = "HTTP socket creation failed: " + socketError();
            return 1;
        }
        if (!setReuseAddress(listener.nativeHandle(), error)) return 1;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(port_);
#ifdef _WIN32
        const auto address_value = ::inet_addr(host_.c_str());
        if (address_value == INADDR_NONE) {
            error = "HTTP host must be an IPv4 address: " + host_;
            return 1;
        }
        address.sin_addr.s_addr = address_value;
#else
        if (::inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1) {
            error = "HTTP host must be an IPv4 address: " + host_;
            return 1;
        }
#endif
        if (::bind(listener.nativeHandle(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            error = "HTTP bind failed: " + socketError();
            return 1;
        }
        if (::listen(listener.nativeHandle(), SOMAXCONN) != 0) {
            error = "HTTP listen failed: " + socketError();
            return 1;
        }
        std::cout << "[server] event=http_listen host=" << host_ << " port=" << port_ << '\n';

        while (!stop_requested_.load()) {
            fd_set read_set;
            FD_ZERO(&read_set);
            FD_SET(listener.nativeHandle(), &read_set);
            timeval timeout{0, 200000};
#ifdef _WIN32
            const int result = ::select(0, &read_set, nullptr, nullptr, &timeout);
#else
            const int result = ::select(listener.nativeHandle() + 1, &read_set, nullptr, nullptr, &timeout);
#endif
            if (result < 0) {
                if (stop_requested_.load()) break;
                error = "HTTP select failed: " + socketError();
                return 1;
            }
            if (result == 0 || !FD_ISSET(listener.nativeHandle(), &read_set)) continue;

            sockaddr_in client_address{};
#ifdef _WIN32
            int address_length = sizeof(client_address);
#else
            socklen_t address_length = sizeof(client_address);
#endif
            const NativeSocket client_socket = ::accept(
                listener.nativeHandle(), reinterpret_cast<sockaddr*>(&client_address), &address_length);
            if (client_socket == kInvalidSocket) continue;
            client_threads_.emplace_back(&AdminApiServer::handleClient, this, client_socket);
        }

        listener.close();
        closeActiveClients();
        for (auto& thread : client_threads_) {
            if (thread.joinable()) thread.join();
        }
        return 0;
    } catch (const std::exception& exception) {
        error = exception.what();
        return 1;
    }
}

}  // namespace miniredis
