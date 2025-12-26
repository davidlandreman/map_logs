#include "mcp_server.hpp"
#include "source_manager.hpp"
#include "server_log.hpp"
#include <chrono>

namespace mcp_logs {

McpServer::McpServer(LogStore& store, SourceManager& sources, HttpServer& http)
    : store_(store), sources_(sources), http_(http)
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

        ServerLog::log("MCP", method + " (session: " + session_id + ")");

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
            {"version", "1.0.0"},
            {"description",
                "Unreal Engine Log Aggregation Server - Collects and queries logs from UE clients and servers.\n\n"
                "DEBUGGING WORKFLOW:\n"
                "1. Start with 'get_stats' or 'logs://stats' to understand error/warning counts\n"
                "2. Use 'logs://errors' to see Fatal and Error level logs immediately\n"
                "3. Use 'get_categories' to discover what subsystems are logging\n"
                "4. Use 'query_logs' with category filter to isolate specific subsystems\n"
                "5. Use 'search_logs' to find specific messages, IDs, or error text\n"
                "6. Use 'get_sessions' to compare behavior across different play sessions\n\n"
                "LOG ENTRY FIELDS:\n"
                "- source: 'client' or 'server' (compare both for networking issues)\n"
                "- category: UE log category (LogTemp, LogNet, LogGameMode, etc.)\n"
                "- verbosity: Fatal > Error > Warning > Display > Log > Verbose\n"
                "- message: The log text - best when it includes structured data\n"
                "- timestamp: UE game time (seconds since engine start)\n"
                "- frame: UE frame number for frame-precise debugging\n"
                "- session_id: Groups logs from same game session across client+server\n"
                "- instance_id: Distinguishes multiple clients in same session\n"
                "- file/line: Source code location (when available)\n\n"
                "BEST PRACTICES FOR GAME DEVELOPERS:\n"
                "Write logs that help AI assistants debug issues:\n"
                "- Include IDs: 'Player=123 killed Enemy=456' not 'Player killed enemy'\n"
                "- Include state: 'Health=45->25 Damage=20 Armor=10' not 'took damage'\n"
                "- Include context: 'Weapon=Rifle Crit=true Headshot=true'\n"
                "- Use specific categories: LogInventory, LogCombat, LogNetwork, LogAI\n"
                "- Use correct verbosity: Error for failures, Warning for recoverable, Log for flow\n"
                "- Call SetSessionId() when matches/levels start to correlate logs"}
        }}
    };
}

nlohmann::json McpServer::handle_tools_list() {
    nlohmann::json tools = nlohmann::json::array();

    // query_logs
    tools.push_back({
        {"name", "query_logs"},
        {"description",
            "Query log entries with filters. Returns latest session's logs by default.\n\n"
            "WHEN TO USE:\n"
            "- Filter by 'category' to isolate subsystems (LogNet for networking, LogAI for AI, etc.)\n"
            "- Filter by 'source' to compare client vs server behavior\n"
            "- Use time range (since/until) to narrow down to reproduction window\n"
            "- Filter by 'verbosity' to focus on errors first, then expand\n"
            "- Compare multiple 'instance_id' values to debug desync between clients\n\n"
            "WORKFLOW: Call get_stats first to understand log distribution, then query specific categories.\n\n"
            "RETURNS: {count, logs[]} where each log has source, category, verbosity, message, timestamp, frame, session_id, instance_id, and optionally file/line."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Filter by 'client' or 'server'. Compare both for networking/replication issues."}}},
                {"verbosity", {{"type", "string"}, {"description", "Minimum severity: Fatal (most severe), Error, Warning, Display, Log, Verbose. Filters to this level and above."}}},
                {"category", {{"type", "string"}, {"description", "Filter by UE log category. Use get_categories to discover available categories. Common: LogTemp, LogNet, LogGameMode, LogPlayerController."}}},
                {"since", {{"type", "number"}, {"description", "Unix timestamp - only logs after this time. Use with 'until' to isolate a time window."}}},
                {"until", {{"type", "number"}, {"description", "Unix timestamp - only logs before this time."}}},
                {"limit", {{"type", "integer"}, {"description", "Maximum results (default: 100). Increase for comprehensive analysis."}}},
                {"session_id", {{"type", "string"}, {"description", "Filter to specific game session. Get session IDs from get_sessions."}}},
                {"instance_id", {{"type", "string"}, {"description", "Filter to specific client/server instance within a session. Useful for debugging specific player's issues."}}},
                {"all_sessions", {{"type", "boolean"}, {"description", "If true, query across all sessions. Default false returns only latest session. Set true to compare behavior across sessions."}}}
            }}
        }}
    });

    // search_logs
    tools.push_back({
        {"name", "search_logs"},
        {"description",
            "Full-text search through log messages using SQLite FTS5. Searches latest session by default.\n\n"
            "QUERY SYNTAX:\n"
            "- Simple: 'player damage' finds logs containing both words\n"
            "- Phrase: '\"player died\"' finds exact phrase\n"
            "- OR: 'error OR warning' finds either\n"
            "- NOT: 'player NOT respawn' excludes respawn\n"
            "- Prefix: 'play*' matches player, playing, etc.\n\n"
            "WHEN TO USE:\n"
            "- Search for entity IDs: 'Player_123' or 'Entity_456'\n"
            "- Find error messages: 'failed OR error OR exception'\n"
            "- Trace specific events: '\"weapon fired\"' or 'damage*'\n"
            "- Find stack traces: 'nullptr OR null OR crash'\n\n"
            "TIP: Combine with 'source' filter to see if issue is client-side, server-side, or both."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"query", {{"type", "string"}, {"description", "FTS5 search query. Use quotes for exact phrases, OR/NOT for boolean logic, * for prefix matching."}}},
                {"source", {{"type", "string"}, {"description", "Filter by 'client' or 'server' to narrow scope."}}},
                {"verbosity", {{"type", "string"}, {"description", "Minimum verbosity level to include in results."}}},
                {"limit", {{"type", "integer"}, {"description", "Maximum results (default: 100)."}}},
                {"session_id", {{"type", "string"}, {"description", "Search within specific session only."}}},
                {"instance_id", {{"type", "string"}, {"description", "Search within specific client/server instance."}}},
                {"all_sessions", {{"type", "boolean"}, {"description", "If true, search across all sessions. Useful for finding recurring issues."}}}
            }},
            {"required", {"query"}}
        }}
    });

    // get_stats
    tools.push_back({
        {"name", "get_stats"},
        {"description",
            "Get log statistics - counts by source, verbosity, and category. RECOMMENDED FIRST STEP.\n\n"
            "USE THIS TO:\n"
            "- Triage: How many errors/warnings exist? Is this a major issue or noise?\n"
            "- Identify hot spots: Which categories have the most logs?\n"
            "- Compare client vs server: Is one side logging more errors?\n"
            "- Track trends: Use 'since' to see stats for recent time window only.\n\n"
            "RETURNS: total_count, client_count, server_count, error_count, warning_count, by_category (top 20), session_count, instance_count, current_session.\n\n"
            "WORKFLOW: Call this first, then drill down into specific categories or error types."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Filter stats to 'client' or 'server' only."}}},
                {"since", {{"type", "number"}, {"description", "Only count logs after this Unix timestamp. Use to see recent activity only."}}}
            }}
        }}
    });

    // get_categories
    tools.push_back({
        {"name", "get_categories"},
        {"description",
            "List all unique UE log categories that have been seen.\n\n"
            "USE THIS TO:\n"
            "- Discover what subsystems are logging (you may not know all category names)\n"
            "- Find the correct category name for query_logs filtering\n"
            "- Understand codebase structure - each category typically maps to a module/subsystem\n\n"
            "COMMON CATEGORIES:\n"
            "- LogTemp: Generic/temporary logs\n"
            "- LogNet, LogNetPlayerMovement: Networking\n"
            "- LogGameMode, LogPlayerController: Gameplay\n"
            "- LogAI, LogBehaviorTree: AI systems\n"
            "- Custom categories: Game-specific (LogInventory, LogCombat, etc.)\n\n"
            "TIP: Compare 'source=client' vs 'source=server' categories to see what each side logs."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Filter to 'client' or 'server' categories only."}}}
            }}
        }}
    });

    // clear_logs
    tools.push_back({
        {"name", "clear_logs"},
        {"description",
            "Delete log entries from the database. DESTRUCTIVE - use with caution.\n\n"
            "WHEN TO USE:\n"
            "- Clear old logs before reproducing a bug for clean capture\n"
            "- Remove noise from previous sessions to focus on current issue\n"
            "- Free up database space after analysis is complete\n\n"
            "SAFETY:\n"
            "- Deleted logs cannot be recovered\n"
            "- Consider exporting important logs before clearing\n"
            "- Use 'before' parameter to only clear old logs\n"
            "- Use 'source' to only clear client or server logs\n\n"
            "RETURNS: {deleted: count, message: string}"},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Only clear 'client' or 'server' logs. Leave empty to clear both."}}},
                {"before", {{"type", "number"}, {"description", "Only clear logs before this Unix timestamp. Use to preserve recent logs."}}}
            }}
        }}
    });

    // tail_logs
    tools.push_back({
        {"name", "tail_logs"},
        {"description",
            "Get the most recent N log entries (like Unix 'tail'). Returns latest session by default.\n\n"
            "WHEN TO USE:\n"
            "- See what just happened - great for 'it just crashed' or 'that was weird' moments\n"
            "- Monitor recent activity during live debugging\n"
            "- Quick check before diving into detailed queries\n"
            "- After reproduction - see the last 50-100 logs leading up to the issue\n\n"
            "WORKFLOW:\n"
            "1. Call tail_logs to see recent context\n"
            "2. If you see relevant errors, use search_logs to find similar patterns\n"
            "3. Use query_logs with filters to investigate further\n\n"
            "TIP: Use 'source' filter to see only client or server logs if debugging networking issues."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"count", {{"type", "integer"}, {"description", "Number of recent logs (default: 50). Increase to 100-200 for more context."}}},
                {"source", {{"type", "string"}, {"description", "Filter to 'client' or 'server' only."}}},
                {"session_id", {{"type", "string"}, {"description", "Get tail of specific session."}}},
                {"instance_id", {{"type", "string"}, {"description", "Get tail of specific client/server instance."}}},
                {"all_sessions", {{"type", "boolean"}, {"description", "If true, tail across all sessions (may mix different play sessions)."}}}
            }}
        }}
    });

    // get_sessions
    tools.push_back({
        {"name", "get_sessions"},
        {"description",
            "List game sessions with metadata including time range, log counts, and participating instances.\n\n"
            "CONCEPTS:\n"
            "- session_id: Groups logs from the same game match/level across multiple clients and server\n"
            "- instance_id: Unique identifier for each running client or server process\n"
            "- A session may have 1 server + N clients, all sharing the same session_id\n\n"
            "WHEN TO USE:\n"
            "- Find specific play sessions to investigate\n"
            "- Compare behavior across different sessions (regression testing)\n"
            "- Identify which session had the bug report\n"
            "- See how many instances participated in each session\n\n"
            "RETURNS PER SESSION:\n"
            "- session_id, first_seen, last_seen (time range)\n"
            "- log_count (total logs in session)\n"
            "- instances[] (list of client/server instance IDs)\n\n"
            "TIP: Use session_id with query_logs or search_logs to focus on a specific play session."},
        {"inputSchema", {
            {"type", "object"},
            {"properties", {
                {"source", {{"type", "string"}, {"description", "Filter to sessions that have 'client' or 'server' logs."}}},
                {"limit", {{"type", "integer"}, {"description", "Max sessions to return (default: 20). Most recent sessions first."}}}
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
        {"description",
            "The 100 most recent log entries from the current session.\n\n"
            "USE FOR: Quick context when starting to debug. See what's been happening recently.\n\n"
            "RETURNS: Array of log entries, each with source, category, verbosity, message, timestamp, frame, session_id, instance_id.\n\n"
            "TIP: Check this first for immediate context, then use tools for detailed filtering."},
        {"mimeType", "application/json"}
    });

    resources.push_back({
        {"uri", "logs://stats"},
        {"name", "Log Statistics"},
        {"description",
            "Current log statistics - counts by source, verbosity, and category.\n\n"
            "USE FOR: RECOMMENDED FIRST STEP. Understand the log landscape before diving in.\n\n"
            "RETURNS: total_count, client_count, server_count, error_count, warning_count, by_category (top 20), session_count, instance_count, current_session.\n\n"
            "WORKFLOW: Check error_count and warning_count first. High numbers indicate problems. Then check by_category to see which subsystems are noisy."},
        {"mimeType", "application/json"}
    });

    resources.push_back({
        {"uri", "logs://errors"},
        {"name", "Error Logs"},
        {"description",
            "Up to 100 most recent Error and Fatal level log entries.\n\n"
            "USE FOR: Immediately focus on problems. Skip the noise and see what's failing.\n\n"
            "RETURNS: Array of Error/Fatal level logs with full context.\n\n"
            "TIP: If this is empty, there are no logged errors. Check Warning level via query_logs if issues persist."},
        {"mimeType", "application/json"}
    });

    resources.push_back({
        {"uri", "logs://current-session"},
        {"name", "Current Session Logs"},
        {"description",
            "Up to 100 logs from the most recent game session (identified by session_id).\n\n"
            "USE FOR: Focus on the current/latest play session. Ignore old test runs.\n\n"
            "RETURNS: {session_id, count, logs[]} - the session ID and its logs.\n\n"
            "TIP: Use the session_id with query_logs or search_logs for more comprehensive session analysis."},
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
