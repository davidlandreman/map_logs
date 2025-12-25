#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <vector>
#include <nlohmann/json.hpp>

namespace mcp_logs {

// Matches UE ELogVerbosity
enum class Verbosity : int {
    NoLogging = 0,
    Fatal = 1,
    Error = 2,
    Warning = 3,
    Display = 4,
    Log = 5,
    Verbose = 6,
    VeryVerbose = 7
};

inline std::string verbosity_to_string(Verbosity v) {
    switch (v) {
        case Verbosity::NoLogging: return "NoLogging";
        case Verbosity::Fatal: return "Fatal";
        case Verbosity::Error: return "Error";
        case Verbosity::Warning: return "Warning";
        case Verbosity::Display: return "Display";
        case Verbosity::Log: return "Log";
        case Verbosity::Verbose: return "Verbose";
        case Verbosity::VeryVerbose: return "VeryVerbose";
        default: return "Unknown";
    }
}

inline Verbosity string_to_verbosity(const std::string& s) {
    if (s == "Fatal") return Verbosity::Fatal;
    if (s == "Error") return Verbosity::Error;
    if (s == "Warning") return Verbosity::Warning;
    if (s == "Display") return Verbosity::Display;
    if (s == "Log") return Verbosity::Log;
    if (s == "Verbose") return Verbosity::Verbose;
    if (s == "VeryVerbose") return Verbosity::VeryVerbose;
    return Verbosity::Log; // Default
}

struct LogEntry {
    int64_t id = 0;                          // Database ID (0 if not persisted)
    std::string source;                       // "client" or "server"
    std::string category;                     // UE log category (e.g., "LogTemp")
    Verbosity verbosity = Verbosity::Log;
    std::string message;
    double timestamp = 0.0;                   // Unix timestamp from UE
    std::optional<int64_t> frame;             // UE frame number
    std::optional<std::string> file;          // Source file
    std::optional<int> line;                  // Source line
    double received_at = 0.0;                 // Server receive timestamp
    std::string session_id;                   // Shared game session identifier
    std::string instance_id;                  // Unique app instance identifier

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["id"] = id;
        j["source"] = source;
        j["category"] = category;
        j["verbosity"] = verbosity_to_string(verbosity);
        j["message"] = message;
        j["timestamp"] = timestamp;
        j["received_at"] = received_at;
        j["session_id"] = session_id;
        j["instance_id"] = instance_id;
        if (frame) j["frame"] = *frame;
        if (file) j["file"] = *file;
        if (line) j["line"] = *line;
        return j;
    }

    static LogEntry from_json(const nlohmann::json& j) {
        LogEntry entry;
        if (j.contains("id")) entry.id = j["id"].get<int64_t>();
        entry.source = j.value("source", "unknown");
        entry.category = j.value("category", "LogTemp");
        entry.verbosity = string_to_verbosity(j.value("verbosity", "Log"));
        entry.message = j.value("message", "");
        entry.timestamp = j.value("timestamp", 0.0);
        entry.received_at = j.value("received_at", 0.0);
        entry.session_id = j.value("session_id", "");
        entry.instance_id = j.value("instance_id", "");
        if (j.contains("frame")) entry.frame = j["frame"].get<int64_t>();
        if (j.contains("file")) entry.file = j["file"].get<std::string>();
        if (j.contains("line")) entry.line = j["line"].get<int>();
        return entry;
    }
};

struct LogFilter {
    std::optional<std::string> source;
    std::optional<Verbosity> min_verbosity;   // Only logs >= this level
    std::optional<std::string> category;
    std::optional<double> since;              // Timestamp >=
    std::optional<double> until;              // Timestamp <=
    std::optional<std::string> session_id;    // Filter to specific session
    std::optional<std::string> instance_id;   // Filter to specific instance
    bool all_sessions = false;                // If false, return only latest session
    int limit = 100;
    int offset = 0;
};

struct LogStats {
    int64_t total_count = 0;
    int64_t client_count = 0;
    int64_t server_count = 0;
    int64_t error_count = 0;     // Error + Fatal
    int64_t warning_count = 0;
    std::map<std::string, int64_t> by_category;
    int64_t session_count = 0;
    int64_t instance_count = 0;
    std::string current_session;

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["total"] = total_count;
        j["client"] = client_count;
        j["server"] = server_count;
        j["errors"] = error_count;
        j["warnings"] = warning_count;
        j["by_category"] = by_category;
        j["session_count"] = session_count;
        j["instance_count"] = instance_count;
        j["current_session"] = current_session;
        return j;
    }
};

struct SessionInfo {
    std::string session_id;
    double first_seen = 0.0;
    double last_seen = 0.0;
    int64_t log_count = 0;
    std::vector<std::string> instances;

    nlohmann::json to_json() const {
        nlohmann::json j;
        j["session_id"] = session_id;
        j["first_seen"] = first_seen;
        j["last_seen"] = last_seen;
        j["log_count"] = log_count;
        j["instances"] = instances;
        return j;
    }
};

} // namespace mcp_logs
