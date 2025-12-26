#pragma once

#include "log_store.hpp"
#include "log_entry.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>

namespace mcp_logs {

// Forward declarations
class ConsoleUI;
class SourceManager;

// Slash command definition
struct SlashCommand {
    std::string name;
    std::string description;
    std::function<void(ConsoleUI&, const std::vector<std::string>&)> handler;
    bool accepts_args = false;  // If true, command can receive arguments
};

// Formatted log line for UDP log display
struct DisplayLogLine {
    std::string category;
    std::string message;
    Verbosity verbosity;
    std::chrono::steady_clock::time_point received_at;
};

// Server log entry (from ServerLog capture)
struct ServerLogLine {
    std::string component;
    std::string message;
    bool is_error;
    std::chrono::steady_clock::time_point timestamp;
};

// Thread-safe circular log buffer
template<typename T>
class LogBuffer {
public:
    explicit LogBuffer(size_t max_lines = 1000);
    void push(T line);
    std::vector<T> get_lines() const;
    size_t size() const;
    void clear();
private:
    mutable std::mutex mutex_;
    std::deque<T> lines_;
    size_t max_lines_;
};

// Statistics for display
struct DisplayStats {
    int64_t total_logs = 0;
    int64_t error_count = 0;
    int64_t warning_count = 0;
    int64_t session_count = 0;
    double logs_per_second = 0.0;
    std::string current_session;
};

// Main TUI class
class ConsoleUI {
public:
    ConsoleUI(LogStore& store, SourceManager& sources, uint16_t udp_port, uint16_t http_port,
              bool is_https, const std::string& db_path);
    ~ConsoleUI();

    // Start the TUI (blocks until exit)
    void run(std::atomic<bool>& running);

    // Called when a new UDP log arrives (from LogStore subscriber)
    void on_udp_log(const LogEntry& entry);

    // Called to add a server log (replaces cout/cerr)
    void log_server(const std::string& component, const std::string& message,
                    bool is_error = false);

    // Get the server log sink for redirecting output
    using ServerLogSink = std::function<void(const std::string&, const std::string&, bool)>;
    ServerLogSink get_log_sink();

private:
    // Rendering helpers
    ftxui::Color verbosity_to_color(Verbosity v);

    // Stats update (called periodically)
    void update_stats();

    // State
    LogStore& store_;
    SourceManager& sources_;
    LogBuffer<DisplayLogLine> udp_logs_;
    LogBuffer<ServerLogLine> server_logs_;
    DisplayStats stats_;
    std::mutex stats_mutex_;

    // UI state
    std::atomic<bool> paused_{false};

    // Command input state
    std::string command_input_;
    std::string completion_hint_;
    std::vector<SlashCommand> commands_;

    // Command handling
    void init_commands(std::atomic<bool>& running, ftxui::ScreenInteractive& screen);
    void execute_command(std::atomic<bool>& running, ftxui::ScreenInteractive& screen);
    void handle_tab_completion();
    std::string complete_command(const std::string& partial);
    void update_completion_hint();

    // Config info for display
    uint16_t udp_port_;
    uint16_t http_port_;
    bool is_https_;
    std::string db_path_;

    // Rate tracking
    std::atomic<int64_t> logs_in_window_{0};
    std::chrono::steady_clock::time_point rate_window_start_;

    // Screen reference for refresh
    ftxui::ScreenInteractive* screen_ = nullptr;
};

} // namespace mcp_logs
