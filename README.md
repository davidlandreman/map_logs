# Map Logs

Multisource log aggregation for agentic AI access.

An MCP (Model Context Protocol) server that aggregates logs from multiple sources. It receives logs via UDP and file tailing, exposing them through an MCP-compliant HTTP/SSE interface. AI assistants like Claude can query, search, and analyze your logs in real-time.

## Features

- **Multi-source log aggregation**: UDP ingestion and file tailing (`tail -f` style)
- **SQLite persistence** with FTS5 full-text search
- **Session tracking** to correlate logs across distributed instances
- **MCP protocol support** for Claude and other MCP clients
- **10 MCP tools**: query, search, tail, stats, categories, sessions, clear, and source management
- **4 MCP resources**: recent logs, stats, errors, current session
- **Modern terminal UI** with live log display and statistics (FTXUI)
- **HTTPS support** for secure MCP connections

## Quick Start

### 1. Build the Server

**Prerequisites:**
- CMake 3.16+
- C++17 compiler
- SQLite3 (system-installed)

```bash
bin/build
```

### 2. Run the Server

```bash
# Basic usage with UDP receiver
bin/run --udp-port 52099 --http-port 52080 --db logs.db

# With file tailing
bin/run --tail /var/log/myapp.log --tail-name "MyApp"

# With HTTPS
bin/run --cert server.pem --key server.key --http-port 52443

# Legacy console mode (no TUI)
bin/run --legacy-console
```

The server starts with a modern terminal UI showing live logs and statistics. Use `--legacy-console` for simple text output.

### 3. Configure Your MCP Client

Add to your Claude Desktop or MCP client configuration:

```json
{
  "mcpServers": {
    "map-logs": {
      "url": "http://localhost:52080/sse"
    }
  }
}
```

---

## Unreal Engine Integration

The server includes a header-only logging component that captures all `UE_LOG` output and sends it to the server via UDP.

### Step 1: Add the Header to Your Project

Copy `unreal/LogServerOutputDevice.h` to your Unreal Engine project's source folder.

### Step 2: Register the Output Device

In your game module or game instance, register the log device during initialization.

**Option A: Game Module (recommended for early initialization)**

```cpp
// YourGame.cpp
#include "LogServerOutputDevice.h"

// Store pointer globally or in your module to access SetSessionId later
static FLogServerOutputDevice* GLogServerDevice = nullptr;

void FYourGameModule::StartupModule()
{
    // Determine source name based on instance type
    FString SourceName = IsRunningDedicatedServer() ? TEXT("server") : TEXT("client");

    // Create and register the output device
    // Parameters: Host, Port, SourceName
    GLogServerDevice = new FLogServerOutputDevice(
        TEXT("127.0.0.1"),  // Log server IP address
        52099,              // UDP port (must match --udp-port)
        SourceName          // "client" or "server"
    );
    GLog->AddOutputDevice(GLogServerDevice);
}

void FYourGameModule::ShutdownModule()
{
    if (GLogServerDevice)
    {
        GLog->RemoveOutputDevice(GLogServerDevice);
        delete GLogServerDevice;
        GLogServerDevice = nullptr;
    }
}
```

**Option B: Game Instance (for per-instance control)**

```cpp
// YourGameInstance.cpp
#include "LogServerOutputDevice.h"

void UYourGameInstance::Init()
{
    Super::Init();

    FString SourceName = IsRunningDedicatedServer() ? TEXT("server") : TEXT("client");
    LogDevice = new FLogServerOutputDevice(TEXT("192.168.1.100"), 52099, SourceName);
    GLog->AddOutputDevice(LogDevice);
}

void UYourGameInstance::Shutdown()
{
    if (LogDevice)
    {
        GLog->RemoveOutputDevice(LogDevice);
        delete LogDevice;
        LogDevice = nullptr;
    }
    Super::Shutdown();
}
```

### Step 3: Set Session ID (Optional but Recommended)

Session IDs let you correlate logs from multiple instances in the same game session (e.g., a match with multiple clients and a server).

```cpp
// When a game session starts (match begins, world is loaded, etc.)
void AYourGameMode::BeginPlay()
{
    Super::BeginPlay();

    // Use any shared identifier: match ID, lobby code, world seed, etc.
    FString SessionId = FString::Printf(TEXT("match_%d"), FMath::RandRange(10000, 99999));

    if (GLogServerDevice)
    {
        GLogServerDevice->SetSessionId(SessionId);
    }

    // Share this SessionId with clients so they can set the same value
}

// On clients, when receiving the session ID from the server:
void AYourPlayerController::OnSessionIdReceived(const FString& SessionId)
{
    if (GLogServerDevice)
    {
        GLogServerDevice->SetSessionId(SessionId);
    }
}
```

### What Gets Logged

Every `UE_LOG` call in your game is automatically captured:

```cpp
UE_LOG(LogTemp, Warning, TEXT("Player health low: %d"), Health);
UE_LOG(LogNet, Error, TEXT("Failed to connect to server"));
UE_LOG(LogMyGame, Display, TEXT("Level loaded: %s"), *LevelName);
```

Each log entry includes:
- **source**: "client" or "server"
- **category**: The log category (LogTemp, LogNet, etc.)
- **verbosity**: Fatal, Error, Warning, Display, Log, Verbose, VeryVerbose
- **message**: The log message text
- **timestamp**: UE time in seconds
- **frame**: Game frame number
- **session_id**: Your session identifier (if set)
- **instance_id**: Auto-generated unique ID per app instance

---

## Generic Source Integration

Map Logs can receive logs from any application that sends JSON via UDP. This makes it useful for aggregating logs from diverse sources: game engines, web servers, CLI tools, scripts, and more.

### UDP Log Format

Send JSON messages to the UDP port (default 52099). Required and optional fields:

```json
{
  "source": "my-app",           // Required: identifies the log source
  "category": "Network",        // Required: log category/module
  "verbosity": "Warning",       // Required: Fatal, Error, Warning, Display, Log, Verbose, VeryVerbose
  "message": "Connection lost", // Required: the log message
  "timestamp": 1234.56,         // Optional: time in seconds
  "frame": 12345,               // Optional: frame/sequence number
  "session_id": "session-abc",  // Optional: correlate logs across sources
  "instance_id": "inst-001"     // Optional: auto-generated if not provided
}
```

### Python Example

```python
import socket
import json

def send_log(message, category="App", verbosity="Log", source="python-app"):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    log_entry = {
        "source": source,
        "category": category,
        "verbosity": verbosity,
        "message": message
    }
    sock.sendto(json.dumps(log_entry).encode(), ("127.0.0.1", 52099))

# Usage
send_log("Application started", category="Startup", verbosity="Display")
send_log("Failed to load config", category="Config", verbosity="Error")
```

### Node.js Example

```javascript
const dgram = require('dgram');

function sendLog(message, category = 'App', verbosity = 'Log', source = 'node-app') {
  const client = dgram.createSocket('udp4');
  const logEntry = JSON.stringify({
    source,
    category,
    verbosity,
    message
  });
  client.send(logEntry, 52099, '127.0.0.1', () => client.close());
}

// Usage
sendLog('Server listening on port 3000', 'HTTP', 'Display');
sendLog('Database connection failed', 'Database', 'Error');
```

### Bash/curl Example (for testing)

```bash
echo '{"source":"test","category":"Test","verbosity":"Log","message":"Hello from bash"}' | nc -u -w0 127.0.0.1 52099
```

### Multi-Source Configuration

When aggregating from multiple sources, use distinct `source` names and shared `session_id` values to correlate related logs:

```
┌──────────────┐     ┌──────────────┐     ┌──────────────┐
│  Frontend    │     │   Backend    │     │   Worker     │
│ source:web   │     │ source:api   │     │ source:job   │
└──────┬───────┘     └──────┬───────┘     └──────┬───────┘
       │                    │                    │
       │    session_id: "request-12345"         │
       └────────────────────┼────────────────────┘
                            │ UDP
                            ▼
                    ┌───────────────┐
                    │  Map Logs     │
                    │   Server      │
                    └───────────────┘
```

---

## File Tailing

Map Logs can monitor local log files, similar to `tail -f`. This is useful for aggregating logs from applications that write to files rather than sending via UDP.

### Command Line

Add file tailers at startup:

```bash
# Single file
bin/run --tail /var/log/app.log

# With custom name (used as category)
bin/run --tail /var/log/app.log --tail-name "Application"

# Multiple files
bin/run --tail /var/log/app.log --tail-name "App" \
        --tail /var/log/nginx/error.log --tail-name "Nginx"
```

### Via MCP Tools

Add or remove file sources dynamically through MCP:

```
add_file_source:
  path: "/var/log/myapp.log"   # Required: path to file
  name: "MyApp"                # Optional: category name (defaults to filename)

remove_source:
  id: "file-1"                 # Source ID from list_sources

list_sources:
  # Returns all active file tailers with their IDs
```

### Behavior

- Starts reading from end of file (like `tail -f`)
- Each line becomes a log entry with:
  - `source`: "file-tailer"
  - `category`: filename or custom name
  - `verbosity`: "Log"
- Handles file rotation (detects size decrease, resets position)
- Recovers if file is deleted and recreated
- Polls every 200ms for new content

---

## MCP Tools Reference

Once connected via MCP, these tools are available:

### query_logs
Retrieve logs with flexible filtering.
```
source: "client" | "server" (optional)
verbosity: minimum level to include (optional)
category: filter by log category (optional)
since/until: timestamp range (optional)
limit: max results, default 100
session_id: filter to specific session (optional)
all_sessions: include all sessions, default false (latest only)
```

### search_logs
Full-text search through log messages. Supports AND, OR, NOT, and "phrase" queries.
```
query: search terms (required)
source, verbosity, limit, session_id, all_sessions: same as query_logs
```

### tail_logs
Get the most recent N log entries.
```
count: number of logs, default 50
source, session_id, instance_id, all_sessions: optional filters
```

### get_stats
Aggregate statistics about logged data.
```
source: filter by source (optional)
since: only count logs after timestamp (optional)
```
Returns: total count, errors, warnings, breakdown by category, session/instance counts.

### get_categories
List all unique log categories seen.
```
source: filter by source (optional)
```

### get_sessions
List all game sessions with metadata.
```
source: filter by source (optional)
limit: max sessions, default 20
```
Returns: session IDs with first_seen, last_seen, log_count, and instances list.

### clear_logs
Delete logs (use with caution).
```
source: only clear this source (optional)
before: only clear logs before timestamp (optional)
```

### add_file_source
Start tailing a new log file.
```
path: absolute path to the file (required)
name: category name for log entries (optional, defaults to filename)
```
Returns: source ID (e.g., "file-1")

### remove_source
Stop tailing a file source.
```
id: source ID from list_sources (required)
```

### list_sources
List all active file tailers.
```
Returns: array of {id, type, path, name} objects
```

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                      Log Sources                                │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │  UDP Sources (JSON via UDP port 52099)                   │   │
│  │  - Unreal Engine (via FLogServerOutputDevice)            │   │
│  │  - Custom applications sending JSON                      │   │
│  ├─────────────────────────────────────────────────────────┤   │
│  │  File Sources (local file tailing)                       │   │
│  │  - Log files monitored like tail -f                      │   │
│  │  - Added via --tail flag or add_file_source tool         │   │
│  └─────────────────────────────────────────────────────────┘   │
└───────────────────────────┬─────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                      Map Logs Server                            │
│  ┌──────────────┐    ┌───────────────┐    ┌─────────────────┐  │
│  │ UdpReceiver  │───▶│               │◀───│  SourceManager  │  │
│  │ (ASIO async) │    │   LogStore    │    │  + FileTailers  │  │
│  └──────────────┘    │   (SQLite)    │    └─────────────────┘  │
│                      │               │                          │
│  ┌──────────────┐    └───────┬───────┘    ┌─────────────────┐  │
│  │  ConsoleUI   │            │            │   HttpServer    │  │
│  │   (FTXUI)    │◀───────────┴───────────▶│ (HTTP/HTTPS)    │  │
│  └──────────────┘                         └────────┬────────┘  │
│                      ┌──────────────┐              │            │
│                      │  McpServer   │◀─────────────┘            │
│                      │ (JSON-RPC)   │                           │
│                      └──────────────┘                           │
└───────────────────────────┬─────────────────────────────────────┘
                            │ HTTP/SSE (port 52080)
                            ▼
┌─────────────────────────────────────────────────────────────────┐
│                      MCP Client (Claude)                        │
│  - Query and search logs                                        │
│  - Manage file sources dynamically                              │
│  - Get statistics and session info                              │
│  - Monitor errors in real-time                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Configuration

### Server Command Line Options

```
--udp-port <port>     UDP port for receiving logs (default: 52099)
--http-port <port>    HTTP/HTTPS port for MCP SSE endpoint (default: 52080)
--db <path>           SQLite database file path (default: logs.db)
--tail <path>         Add a file tailer source (can be repeated)
--tail-name <name>    Name for the preceding --tail source
--cert <path>         TLS certificate file (PEM) for HTTPS
--key <path>          TLS private key file (PEM) for HTTPS
--legacy-console      Use simple text output instead of TUI
--help                Show usage information
```

### Network Considerations

- **Local development**: Use `127.0.0.1` for both server and UE
- **LAN testing**: Use the server machine's LAN IP (e.g., `192.168.1.100`)
- **Remote server**: Ensure UDP port is accessible through firewalls

---

## Development

### Running Tests

```bash
bin/build && ctest --test-dir build --output-on-failure
```

### Run a Specific Test

```bash
ctest --test-dir build -R "test_name_pattern"
```

### Dependencies

System dependencies (must be installed):
- **SQLite3** - Log storage
- **OpenSSL** - HTTPS support (optional, for --cert/--key)

Fetched automatically via CMake FetchContent:
- **nlohmann/json** - JSON parsing
- **cpp-httplib** - HTTP/HTTPS server
- **asio** (standalone) - Async UDP
- **FTXUI** - Terminal UI
- **Catch2** - Testing

---

## License

See LICENSE file for details.
