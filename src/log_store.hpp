#pragma once

#include "log_entry.hpp"
#include <sqlite3.h>
#include <string>
#include <vector>
#include <mutex>
#include <functional>

namespace mcp_logs {

class LogStore {
public:
    explicit LogStore(const std::string& db_path);
    ~LogStore();

    // Non-copyable
    LogStore(const LogStore&) = delete;
    LogStore& operator=(const LogStore&) = delete;

    // Insert a new log entry, returns the assigned ID
    int64_t insert(const LogEntry& entry);

    // Query logs with filters
    std::vector<LogEntry> query(const LogFilter& filter);

    // Full-text search
    std::vector<LogEntry> search(const std::string& query, const LogFilter& filter);

    // Get statistics
    LogStats get_stats(std::optional<std::string> source = std::nullopt,
                       std::optional<double> since = std::nullopt);

    // Get distinct categories
    std::vector<std::string> get_categories(std::optional<std::string> source = std::nullopt);

    // Get list of sessions
    std::vector<SessionInfo> get_sessions(std::optional<std::string> source = std::nullopt);

    // Get the latest session ID
    std::string get_latest_session(std::optional<std::string> source = std::nullopt);

    // Delete logs matching filter
    int64_t clear(std::optional<std::string> source = std::nullopt,
                  std::optional<double> before = std::nullopt);

    // Get total log count
    int64_t count();

    // Subscribe to new log entries (called from insert)
    using LogCallback = std::function<void(const LogEntry&)>;
    void subscribe(LogCallback callback);

private:
    void init_schema();
    void exec(const std::string& sql);
    LogEntry row_to_entry(sqlite3_stmt* stmt);

    sqlite3* db_ = nullptr;
    std::mutex mutex_;
    std::vector<LogCallback> subscribers_;
};

} // namespace mcp_logs
