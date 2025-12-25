#include "log_store.hpp"
#include "udp_receiver.hpp"
#include "http_server.hpp"
#include "mcp_server.hpp"

#include <iostream>
#include <csignal>
#include <atomic>
#include <string>
#include <cstdlib>

using namespace mcp_logs;

std::atomic<bool> running{true};

void signal_handler(int signal) {
    std::cout << "\n[Main] Shutting down..." << std::endl;
    running = false;
}

void print_usage(const char* program) {
    std::cout << "UE Log Server - Unreal Engine log aggregator with MCP access\n\n";
    std::cout << "Usage: " << program << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --udp-port PORT   UDP port for receiving logs (default: 9999)\n";
    std::cout << "  --http-port PORT  HTTP port for MCP SSE server (default: 8080)\n";
    std::cout << "  --db PATH         SQLite database path (default: logs.db)\n";
    std::cout << "  --help            Show this help message\n\n";
    std::cout << "Example:\n";
    std::cout << "  " << program << " --udp-port 9999 --http-port 8080 --db ue_logs.db\n";
}

int main(int argc, char* argv[]) {
    uint16_t udp_port = 9999;
    uint16_t http_port = 8080;
    std::string db_path = "logs.db";

    // Parse command line arguments
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
        else if (arg == "--udp-port" && i + 1 < argc) {
            udp_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (arg == "--http-port" && i + 1 < argc) {
            http_port = static_cast<uint16_t>(std::stoi(argv[++i]));
        }
        else if (arg == "--db" && i + 1 < argc) {
            db_path = argv[++i];
        }
        else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    // Setup signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        std::cout << "=== UE Log Server ===" << std::endl;
        std::cout << "Database: " << db_path << std::endl;

        // Initialize components
        LogStore store(db_path);
        std::cout << "[Store] Initialized with " << store.count() << " existing logs" << std::endl;

        UdpReceiver udp(store, udp_port);
        HttpServer http(http_port);
        McpServer mcp(store, http);

        // Start services
        udp.start();
        http.start();

        std::cout << "\nServer ready. Press Ctrl+C to stop.\n" << std::endl;
        std::cout << "MCP endpoint: http://localhost:" << http_port << "/sse" << std::endl;
        std::cout << "UDP logs:     localhost:" << udp_port << std::endl;

        // Main loop
        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Cleanup
        std::cout << "[Main] Stopping services..." << std::endl;
        udp.stop();
        http.stop();

        std::cout << "[Main] Shutdown complete. Total logs: " << store.count() << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
