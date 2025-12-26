#pragma once

#include "log_store.hpp"
#include "http_server.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <map>
#include <functional>

namespace mcp_logs {

class SourceManager;

class McpServer {
public:
    McpServer(LogStore& store, SourceManager& sources, HttpServer& http);

    // Handle incoming MCP JSON-RPC request
    nlohmann::json handle_request(const nlohmann::json& request, const std::string& session_id);

private:
    // JSON-RPC response helpers
    nlohmann::json success_response(const nlohmann::json& id, const nlohmann::json& result);
    nlohmann::json error_response(const nlohmann::json& id, int code, const std::string& message);

    // MCP protocol handlers
    nlohmann::json handle_initialize(const nlohmann::json& params);
    nlohmann::json handle_tools_list();
    nlohmann::json handle_tools_call(const nlohmann::json& params);
    nlohmann::json handle_resources_list();
    nlohmann::json handle_resources_read(const nlohmann::json& params);

    // Tool implementations
    nlohmann::json tool_query_logs(const nlohmann::json& args);
    nlohmann::json tool_search_logs(const nlohmann::json& args);
    nlohmann::json tool_get_stats(const nlohmann::json& args);
    nlohmann::json tool_get_categories(const nlohmann::json& args);
    nlohmann::json tool_clear_logs(const nlohmann::json& args);
    nlohmann::json tool_tail_logs(const nlohmann::json& args);
    nlohmann::json tool_get_sessions(const nlohmann::json& args);

    // Source management tools
    nlohmann::json tool_add_file_source(const nlohmann::json& args);
    nlohmann::json tool_remove_source(const nlohmann::json& args);
    nlohmann::json tool_list_sources(const nlohmann::json& args);

    // Resource implementations
    nlohmann::json resource_recent_logs();
    nlohmann::json resource_stats();
    nlohmann::json resource_errors();
    nlohmann::json resource_current_session();

    LogStore& store_;
    SourceManager& sources_;
    HttpServer& http_;
};

} // namespace mcp_logs
