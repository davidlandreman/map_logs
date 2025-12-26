#pragma once

#include <string>
#include <functional>
#include <mutex>

namespace mcp_logs {

// Global server log sink - can be set to TUI or legacy console
class ServerLog {
public:
    using Sink = std::function<void(const std::string& component,
                                     const std::string& message,
                                     bool is_error)>;

    static void set_sink(Sink sink);
    static void log(const std::string& component, const std::string& message);
    static void error(const std::string& component, const std::string& message);

    // Legacy sink that writes to cout/cerr
    static void legacy_sink(const std::string& component,
                            const std::string& message, bool is_error);

private:
    static Sink sink_;
    static std::mutex mutex_;
};

} // namespace mcp_logs
