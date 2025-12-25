#include "http_server.hpp"
#include <sstream>
#include <iomanip>
#include <random>

namespace mcp_logs {

HttpServer::HttpServer(uint16_t port) : port_(port) {
    setup_routes();
}

HttpServer::~HttpServer() {
    stop();
}

std::string HttpServer::generate_session_id() {
    std::stringstream ss;
    ss << "session_" << ++session_counter_ << "_";

    // Add random suffix
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    const char* hex = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        ss << hex[dis(gen)];
    }

    return ss.str();
}

void HttpServer::setup_routes() {
    // Health check
    server_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(R"({"status":"ok"})", "application/json");
    });

    // SSE endpoint for MCP
    server_.Get("/sse", [this](const httplib::Request& req, httplib::Response& res) {
        std::string session_id = generate_session_id();
        std::cout << "[HTTP] SSE client connected: " << session_id << std::endl;

        res.set_header("Cache-Control", "no-cache");
        res.set_header("Connection", "keep-alive");
        res.set_header("Access-Control-Allow-Origin", "*");

        res.set_chunked_content_provider(
            "text/event-stream",
            [this, session_id](size_t offset, httplib::DataSink& sink) -> bool {
                // Register this client
                auto client = std::make_shared<SseClient>();
                client->session_id = session_id;
                client->sink = &sink;

                {
                    std::lock_guard<std::mutex> lock(sse_mutex_);
                    sse_clients_.push_back(client);
                }

                // Send initial endpoint event per MCP spec
                nlohmann::json endpoint_event;
                endpoint_event["endpoint"] = "/messages?session_id=" + session_id;

                std::stringstream ss;
                ss << "event: endpoint\n";
                ss << "data: " << endpoint_event.dump() << "\n\n";
                sink.write(ss.str().c_str(), ss.str().size());

                // Keep connection alive until client disconnects
                while (running_ && sink.is_writable()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }

                // Remove client on disconnect
                {
                    std::lock_guard<std::mutex> lock(sse_mutex_);
                    sse_clients_.erase(
                        std::remove_if(sse_clients_.begin(), sse_clients_.end(),
                            [&session_id](const auto& c) { return c->session_id == session_id; }),
                        sse_clients_.end()
                    );
                }

                std::cout << "[HTTP] SSE client disconnected: " << session_id << std::endl;
                return false;
            }
        );
    });

    // MCP message endpoint
    server_.Post("/messages", [this](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");

        std::string session_id = req.get_param_value("session_id");
        if (session_id.empty()) {
            res.status = 400;
            res.set_content(R"({"error":"Missing session_id"})", "application/json");
            return;
        }

        try {
            auto request_json = nlohmann::json::parse(req.body);

            if (message_handler_) {
                auto response_json = message_handler_(request_json, session_id);

                // Send response via SSE
                std::lock_guard<std::mutex> lock(sse_mutex_);
                for (auto& client : sse_clients_) {
                    if (client->session_id == session_id && client->sink->is_writable()) {
                        std::stringstream ss;
                        ss << "event: message\n";
                        ss << "data: " << response_json.dump() << "\n\n";
                        client->sink->write(ss.str().c_str(), ss.str().size());
                    }
                }
            }

            res.status = 202;  // Accepted
            res.set_content(R"({"status":"accepted"})", "application/json");

        } catch (const std::exception& e) {
            res.status = 400;
            nlohmann::json error;
            error["error"] = e.what();
            res.set_content(error.dump(), "application/json");
        }
    });

    // CORS preflight
    server_.Options("/messages", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });
}

void HttpServer::start() {
    if (running_) return;
    running_ = true;

    thread_ = std::thread([this]() {
        std::cout << "[HTTP] Server starting on port " << port_ << std::endl;
        server_.listen("0.0.0.0", port_);
    });

    // Give server time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void HttpServer::stop() {
    if (!running_) return;
    running_ = false;
    server_.stop();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void HttpServer::broadcast_sse(const std::string& event_type, const nlohmann::json& data) {
    std::lock_guard<std::mutex> lock(sse_mutex_);

    std::stringstream ss;
    ss << "event: " << event_type << "\n";
    ss << "data: " << data.dump() << "\n\n";
    std::string message = ss.str();

    for (auto& client : sse_clients_) {
        if (client->sink->is_writable()) {
            client->sink->write(message.c_str(), message.size());
        }
    }
}

} // namespace mcp_logs
