#include "mcp_server.hpp"
#include <iostream>
#include <chrono>

namespace mcp_logs {

McpServer::McpServer(LogStore& store, HttpServer& http)
    : store_(store), http_(http)
{
    http_.set_message_handler([this](const nlohmann::json& req, const std::string& session_id) {
        return handle_request(req, session_id);
    });
}

nlohmann::json McpServer::success_response(const nlohmann::json& id, const nlohmann::json& result) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", result}
    };
}

nlohmann::json McpServer::error_response(const nlohmann::json& id, int code, const std::string& message) {
    return {
        {"jsonrpc", "2.0"},
        {"id", id},
        {"error", {{"code", code}, {"message", message}}}
    };
}

nlohmann::json McpServer::handle_request(const nlohmann::json& request, const std::string& session_id) {
    try {
        std::string method = request.value("method", "");
        nlohmann::json id = request.value("id", nlohmann::json());
        nlohmann::json params = request.value("params", nlohmann::json::object());

        std::cout << "[MCP] " << method << " (session: " << session_id << ")" << std::endl;

        if (method == "initialize") {
            return success_response(id, handle_initialize(params));
        }
        else if (method == "notifications/initialized") {
            // Client acknowledgment, no response needed for notification
            return nlohmann::json();
        }
        else if (method == "tools/list") {
            return success_response(id, handle_tools_list());
        }
        else if (method == "tools/call") {
            return success_response(id, handle_tools_call(params));
        }
        else if (method == "resources/list") {
            return success_response(id, handle_resources_list());
        }
        else if (method == "resources/read") {
            return success_response(id, handle_resources_read(params));
        }
        else if (method == "ping") {
            return success_response(id, nlohmann::json::object());
        }
        else {
            return error_response(id, -32601, "Method not found: " + method);
        }

    } catch (const std::exception& e) {
        return error_response(request.value("id", nullptr), -32603, e.what());
    }
}

nlohmann::json McpServer::handle_initialize(const nlohmann::json& params) {
    return {
        {"protocolVersion", "2024-11-05"},
        {"capabilities", {
            {"tools", nlohmann::json::object()},
            {"resources", {{"subscribe", false}}}
        }},
        {"serverInfo", {
            {"name", "ue-log-server"},
            {"version", "1.0.0"}
        }}
    };
}

nlohmann::json McpServer::handle_tools_list() {
    nlohmann::json tools = nlohmann::json::array();

    // query_logs
    tools.push_back({
        {"name", "query_logs"},
        {"description", "Query log entries with filters. By default returns only the latest session's logs."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Filter by source: 'client' or 'server'"}}},
                {"verbosity", {{"type", "string"}, {"description", "Minimum verbosity level: Fatal, Error, Warning, Display, Log, Verbose"}}},
                {"category", {{"type", "string"}, {"description", "Filter by UE log category (e.g., 'LogTemp')"}}},
                {"since", {{"type", "number"}, {"description", "Unix timestamp - only logs after this time"}}},
                {"until", {{"type", "number"}, {"description", "Unix timestamp - only logs before this time"}}},
                {"limit", {{"type", "integer"}, {"description", "Maximum number of results (default: 100)"}}},
                {"session_id", {{"type", "string"}, {"description", "Filter to specific session"}}},
                {"instance_id", {{"type", "string"}, {"description", "Filter to specific instance within session"}}},
                {"all_sessions", {{"type", "boolean"}, {"description", "If true, return logs from all sessions (default: false, latest session only)"}}}
            }}
        }}
    });

    // search_logs
    tools.push_back({
        {"name", "search_logs"},
        {"description", "Full-text search through log messages. By default searches only the latest session."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "Search query (supports AND, OR, NOT, phrases in quotes)"}}},
                {"source", {{"type", "string"}, {"description", "Filter by source: 'client' or 'server'"}}},
                {"verbosity", {{"type", "string"}, {"description", "Minimum verbosity level"}}},
                {"limit", {{"type", "integer"}, {"description", "Maximum number of results (default: 100)"}}},
                {"session_id", {{"type", "string"}, {"description", "Filter to specific session"}}},
                {"instance_id", {{"type", "string"}, {"description", "Filter to specific instance within session"}}},
                {"all_sessions", {{"type", "boolean"}, {"description", "If true, search all sessions (default: false)"}}}
            }},
            {"required", {"query"}}
        }}
    });

    // get_stats
    tools.push_back({
        {"name", "get_stats"},
        {"description", "Get log statistics including counts by source, verbosity, and category."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Filter stats by source"}}},
                {"since", {{"type", "number"}, {"description", "Only count logs after this timestamp"}}}
            }}
        }}
    });

    // get_categories
    tools.push_back({
        {"name", "get_categories"},
        {"description", "List all unique log categories that have been seen."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Filter by source"}}}
            }}
        }}
    });

    // clear_logs
    tools.push_back({
        {"name", "clear_logs"},
        {"description", "Delete log entries. Use with caution."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Only clear logs from this source"}}},
                {"before", {{"type", "number"}, {"description", "Only clear logs before this timestamp"}}}
            }}
        }}
    });

    // tail_logs
    tools.push_back({
        {"name", "tail_logs"},
        {"description", "Get the most recent N log entries. By default returns only the latest session's logs."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"count", {{"type", "integer"}, {"description", "Number of recent logs to retrieve (default: 50)"}}},
                {"source", {{"type", "string"}, {"description", "Filter by source"}}},
                {"session_id", {{"type", "string"}, {"description", "Filter to specific session"}}},
                {"instance_id", {{"type", "string"}, {"description", "Filter to specific instance within session"}}},
                {"all_sessions", {{"type", "boolean"}, {"description", "If true, include all sessions (default: false)"}}}
            }}
        }}
    });

    // get_sessions
    tools.push_back({
        {"name", "get_sessions"},
        {"description", "List all game sessions with metadata including log counts and instances."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Filter by source: 'client' or 'server'"}}},
                {"limit", {{"type", "integer"}, {"description", "Maximum number of sessions to return (default: 20)"}}}
            }}
        }}
    });

    return {{"tools", tools}};
}

nlohmann::json McpServer::handle_tools_call(const nlohmann::json& params) {
    std::string name = params.value("name", "");
    nlohmann::json args = params.value("arguments", nlohmann::json::object());

    nlohmann::json result;
    bool is_error = false;

    try {
        if (name == "query_logs") {
            result = tool_query_logs(args);
        }
        else if (name == "search_logs") {
            result = tool_search_logs(args);
        }
        else if (name == "get_stats") {
            result = tool_get_stats(args);
        }
        else if (name == "get_categories") {
            result = tool_get_categories(args);
        }
        else if (name == "clear_logs") {
            result = tool_clear_logs(args);
        }
        else if (name == "tail_logs") {
            result = tool_tail_logs(args);
        }
        else if (name == "get_sessions") {
            result = tool_get_sessions(args);
        }
        else {
            is_error = true;
            result = "Unknown tool: " + name;
        }
    } catch (const std::exception& e) {
        is_error = true;
        result = std::string("Error: ") + e.what();
    }

    nlohmann::json content = nlohmann::json::array();
    content.push_back({
        {"type", "text"},
        {"text", result.dump(2)}
    });

    return {
        {"content", content},
        {"isError", is_error}
    };
}

nlohmann::json McpServer::handle_resources_list() {
    nlohmann::json resources = nlohmann::json::array();

    resources.push_back({
        {"uri", "logs://recent"},
        {"name", "Recent Logs"},
        {"description", "The 100 most recent log entries"},
        {"mimeType", "application/json"}
    });

    resources.push_back({
        {"uri", "logs://stats"},
        {"name", "Log Statistics"},
        {"description", "Current log statistics and counts"},
        {"mimeType", "application/json"}
    });

    resources.push_back({
        {"uri", "logs://errors"},
        {"name", "Error Logs"},
        {"description", "Recent Error and Fatal log entries"},
        {"mimeType", "application/json"}
    });

    resources.push_back({
        {"uri", "logs://current-session"},
        {"name", "Current Session Logs"},
        {"description", "Logs from the most recent game session"},
        {"mimeType", "application/json"}
    });

    return {{"resources", resources}};
}

nlohmann::json McpServer::handle_resources_read(const nlohmann::json& params) {
    std::string uri = params.value("uri", "");

    nlohmann::json result;

    if (uri == "logs://recent") {
        result = resource_recent_logs();
    }
    else if (uri == "logs://stats") {
        result = resource_stats();
    }
    else if (uri == "logs://errors") {
        result = resource_errors();
    }
    else if (uri == "logs://current-session") {
        result = resource_current_session();
    }
    else {
        throw std::runtime_error("Unknown resource: " + uri);
    }

    nlohmann::json contents = nlohmann::json::array();
    contents.push_back({
        {"uri", uri},
        {"mimeType", "application/json"},
        {"text", result.dump(2)}
    });

    return {{"contents", contents}};
}

// Tool implementations

nlohmann::json McpServer::tool_query_logs(const nlohmann::json& args) {
    LogFilter filter;

    if (args.contains("source")) filter.source = args["source"].get<std::string>();
    if (args.contains("category")) filter.category = args["category"].get<std::string>();
    if (args.contains("since")) filter.since = args["since"].get<double>();
    if (args.contains("until")) filter.until = args["until"].get<double>();
    if (args.contains("limit")) filter.limit = args["limit"].get<int>();
    if (args.contains("verbosity")) {
        filter.min_verbosity = string_to_verbosity(args["verbosity"].get<std::string>());
    }
    if (args.contains("session_id")) filter.session_id = args["session_id"].get<std::string>();
    if (args.contains("instance_id")) filter.instance_id = args["instance_id"].get<std::string>();
    if (args.contains("all_sessions")) filter.all_sessions = args["all_sessions"].get<bool>();

    auto logs = store_.query(filter);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& log : logs) {
        result.push_back(log.to_json());
    }

    return {
        {"count", logs.size()},
        {"logs", result}
    };
}

nlohmann::json McpServer::tool_search_logs(const nlohmann::json& args) {
    std::string query = args.value("query", "");
    if (query.empty()) {
        throw std::runtime_error("Query parameter is required");
    }

    LogFilter filter;
    if (args.contains("source")) filter.source = args["source"].get<std::string>();
    if (args.contains("limit")) filter.limit = args["limit"].get<int>();
    if (args.contains("verbosity")) {
        filter.min_verbosity = string_to_verbosity(args["verbosity"].get<std::string>());
    }
    if (args.contains("session_id")) filter.session_id = args["session_id"].get<std::string>();
    if (args.contains("instance_id")) filter.instance_id = args["instance_id"].get<std::string>();
    if (args.contains("all_sessions")) filter.all_sessions = args["all_sessions"].get<bool>();

    auto logs = store_.search(query, filter);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& log : logs) {
        result.push_back(log.to_json());
    }

    return {
        {"count", logs.size()},
        {"query", query},
        {"logs", result}
    };
}

nlohmann::json McpServer::tool_get_stats(const nlohmann::json& args) {
    std::optional<std::string> source;
    std::optional<double> since;

    if (args.contains("source")) source = args["source"].get<std::string>();
    if (args.contains("since")) since = args["since"].get<double>();

    return store_.get_stats(source, since).to_json();
}

nlohmann::json McpServer::tool_get_categories(const nlohmann::json& args) {
    std::optional<std::string> source;
    if (args.contains("source")) source = args["source"].get<std::string>();

    auto categories = store_.get_categories(source);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& cat : categories) {
        result.push_back(cat);
    }

    return {{"categories", result}};
}

nlohmann::json McpServer::tool_clear_logs(const nlohmann::json& args) {
    std::optional<std::string> source;
    std::optional<double> before;

    if (args.contains("source")) source = args["source"].get<std::string>();
    if (args.contains("before")) before = args["before"].get<double>();

    int64_t deleted = store_.clear(source, before);

    return {
        {"deleted", deleted},
        {"message", std::to_string(deleted) + " log entries deleted"}
    };
}

nlohmann::json McpServer::tool_tail_logs(const nlohmann::json& args) {
    int count = args.value("count", 50);

    LogFilter filter;
    filter.limit = count;
    if (args.contains("source")) filter.source = args["source"].get<std::string>();
    if (args.contains("session_id")) filter.session_id = args["session_id"].get<std::string>();
    if (args.contains("instance_id")) filter.instance_id = args["instance_id"].get<std::string>();
    if (args.contains("all_sessions")) filter.all_sessions = args["all_sessions"].get<bool>();

    auto logs = store_.query(filter);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& log : logs) {
        result.push_back(log.to_json());
    }

    return {
        {"count", logs.size()},
        {"logs", result}
    };
}

nlohmann::json McpServer::tool_get_sessions(const nlohmann::json& args) {
    std::optional<std::string> source;
    int limit = args.value("limit", 20);

    if (args.contains("source")) source = args["source"].get<std::string>();

    auto sessions = store_.get_sessions(source);

    // Apply limit
    if (sessions.size() > static_cast<size_t>(limit)) {
        sessions.resize(limit);
    }

    nlohmann::json result = nlohmann::json::array();
    for (const auto& session : sessions) {
        result.push_back(session.to_json());
    }

    return {
        {"count", result.size()},
        {"sessions", result}
    };
}

// Resource implementations

nlohmann::json McpServer::resource_recent_logs() {
    LogFilter filter;
    filter.limit = 100;

    auto logs = store_.query(filter);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& log : logs) {
        result.push_back(log.to_json());
    }

    return result;
}

nlohmann::json McpServer::resource_stats() {
    return store_.get_stats().to_json();
}

nlohmann::json McpServer::resource_errors() {
    LogFilter filter;
    filter.min_verbosity = Verbosity::Error;
    filter.limit = 100;

    auto logs = store_.query(filter);

    nlohmann::json result = nlohmann::json::array();
    for (const auto& log : logs) {
        result.push_back(log.to_json());
    }

    return result;
}

nlohmann::json McpServer::resource_current_session() {
    LogFilter filter;
    filter.limit = 100;
    // By default, filter is already set to latest session (all_sessions = false)

    auto logs = store_.query(filter);
    auto session_id = store_.get_latest_session();

    nlohmann::json logs_json = nlohmann::json::array();
    for (const auto& log : logs) {
        logs_json.push_back(log.to_json());
    }

    return {
        {"session_id", session_id},
        {"count", logs.size()},
        {"logs", logs_json}
    };
}

} // namespace mcp_logs
