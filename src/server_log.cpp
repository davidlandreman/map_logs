#include "server_log.hpp"
#include <iostream>

namespace mcp_logs {

ServerLog::Sink ServerLog::sink_ = ServerLog::legacy_sink;
std::mutex ServerLog::mutex_;

void ServerLog::set_sink(Sink sink) {
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = sink ? std::move(sink) : legacy_sink;
}

void ServerLog::log(const std::string& component, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sink_) {
        sink_(component, message, false);
    }
}

void ServerLog::error(const std::string& component, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sink_) {
        sink_(component, message, true);
    }
}

void ServerLog::legacy_sink(const std::string& component,
                             const std::string& message, bool is_error) {
    if (is_error) {
        std::cerr << "[" << component << "] " << message << std::endl;
    } else {
        std::cout << "[" << component << "] " << message << std::endl;
    }
}

} // namespace mcp_logs
