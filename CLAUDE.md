# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Build
bin/build

# Run the server (pass any flags after)
bin/run --udp-port 52099 --http-port 52080 --db logs.db

# Run with file tailing
bin/run --tail /var/log/app.log --tail-name "AppLog"

# Run with HTTPS
bin/run --cert server.pem --key server.key --http-port 52443

# Run with legacy console (no TUI)
bin/run --legacy-console

# Run tests
bin/build && ctest --test-dir build --output-on-failure

# Run a single test by name
ctest --test-dir build -R "test_name_pattern"
```

## Architecture

This is an MCP (Model Context Protocol) server for aggregating logs from multiple sources. It supports:
- **UDP ingestion** for real-time logs from applications (Unreal Engine, custom apps)
- **File tailing** for monitoring local log files (like `tail -f`)
- **HTTP/SSE interface** for MCP clients to query and subscribe

### Data Flow
```
[UDP Sources] ──────> [UdpReceiver] ──┐
[File Sources] ─────> [FileTailer] ───┼──> [LogStore] ──> [McpServer]
                      [SourceManager] ┘    (SQLite)       (JSON-RPC)
                                                              │
                                           [HttpServer] <─────┘
                                           (HTTP/HTTPS SSE)
                                                 │
                                           [MCP Client]
```

### Key Components
- `LogEntry` (log_entry.hpp): Core data structure with source, category, verbosity, message, session/instance IDs
- `LogStore` (log_store.hpp/cpp): SQLite-backed storage with FTS5 full-text search and subscriber pattern
- `SourceManager` (source_manager.hpp/cpp): Manages active log sources (file tailers, etc.)
- `FileTailer` (file_tailer.hpp/cpp): Monitors files for new lines, handles rotation
- `McpServer` (mcp_server.hpp/cpp): MCP protocol with 10 tools and 4 resources
- `HttpServer` (http_server.hpp/cpp): HTTP/HTTPS with SSE for MCP transport
- `ConsoleUI` (console_ui.hpp/cpp): FTXUI-based terminal interface with live log display
- `ServerLog` (server_log.hpp/cpp): Internal logging with sink pattern for TUI/legacy modes

### MCP Tools
- `query_logs`, `search_logs`, `tail_logs` - Log retrieval and search
- `get_stats`, `get_categories`, `get_sessions` - Metadata queries
- `clear_logs` - Delete logs with filters
- `add_file_source`, `remove_source`, `list_sources` - Dynamic source management

### Source Types
- **UDP**: Any app sending JSON to the UDP port (Unreal Engine via `FLogServerOutputDevice`)
- **File Tailer**: Local files monitored for new lines (`--tail` flag or `add_file_source` tool)

## Dependencies

External libraries are fetched via CMake FetchContent:
- nlohmann/json (JSON parsing)
- cpp-httplib (HTTP/HTTPS server)
- asio (standalone, for UDP)
- FTXUI (terminal UI)
- Catch2 (testing)

System dependencies:
- SQLite3
- OpenSSL (for HTTPS support)
