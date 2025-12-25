#pragma once

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <functional>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include <atomic>
#include <iostream>

namespace mcp_logs {

// Forward declaration
class McpServer;

class HttpServer {
public:
    using MessageHandler = std::function<nlohmann::json(const nlohmann::json&, const std::string&)>;

    explicit HttpServer(uint16_t port = 8080);
    ~HttpServer();

    void set_message_handler(MessageHandler handler) { message_handler_ = std::move(handler); }
    void start();
    void stop();

    // Send an SSE event to all connected clients
    void broadcast_sse(const std::string& event_type, const nlohmann::json& data);

    // Get the next session ID
    std::string generate_session_id();

private:
    void setup_routes();

    httplib::Server server_;
    uint16_t port_;
    std::thread thread_;
    std::atomic<bool> running_{false};

    MessageHandler message_handler_;

    // SSE client management
    struct SseClient {
        std::string session_id;
        httplib::DataSink* sink;
    };
    std::mutex sse_mutex_;
    std::vector<std::shared_ptr<SseClient>> sse_clients_;
    std::atomic<uint64_t> session_counter_{0};
};

} // namespace mcp_logs
