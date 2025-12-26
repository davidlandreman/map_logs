# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Build
bin/build

# Run the server (pass any flags after)
bin/run --udp-port 52099 --http-port 52080 --db logs.db

# Run tests
bin/build && ctest --test-dir build --output-on-failure

# Run a single test by name
ctest --test-dir build -R "test_name_pattern"
```

## Architecture

This is an MCP (Model Context Protocol) server for aggregating logs from multiple sources. It receives logs via UDP and exposes them through an MCP-compliant HTTP/SSE interface.

### Data Flow
1. **UdpReceiver** listens on UDP port (default 52099) for JSON log messages from sources
2. **LogStore** persists logs to SQLite with FTS5 full-text search
3. **HttpServer** provides SSE endpoint for MCP clients at `/sse`
4. **McpServer** handles MCP JSON-RPC protocol, exposing tools and resources

### Key Components
- `LogEntry` (log_entry.hpp): Core data structure for log entries with verbosity levels
- `LogStore` (log_store.hpp/cpp): SQLite-backed storage with subscriber pattern for real-time updates
- `McpServer` (mcp_server.hpp/cpp): MCP protocol implementation with 6 tools (query_logs, search_logs, get_stats, get_categories, clear_logs, tail_logs) and 3 resources

### Source Integrations
- **Unreal Engine**: `unreal/LogServerOutputDevice.h` provides an `FOutputDevice` implementation that sends UE_LOG output to this server via UDP. Register it with `GLog->AddOutputDevice()` in your game module.
- **Custom Sources**: Any application can send JSON-formatted log messages via UDP.

## Dependencies

External libraries are fetched via CMake FetchContent:
- nlohmann/json (JSON parsing)
- cpp-httplib (HTTP server)
- asio (standalone, for UDP)
- Catch2 (testing)

SQLite3 must be installed on the system.
