#include "file_tailer.hpp"
#include <chrono>
#include <sstream>

namespace mcp_logs {

FileTailer::FileTailer(LogStore& store, const std::string& path, const std::string& source_name)
    : store_(store)
    , path_(path)
    , source_name_(source_name.empty() ? extract_filename(path) : source_name)
{
}

FileTailer::~FileTailer() {
    stop();
}

std::string FileTailer::extract_filename(const std::string& path) const {
    std::filesystem::path p(path);
    return p.filename().string();
}

void FileTailer::start() {
    if (running_) return;

    // Check if file exists
    if (!std::filesystem::exists(path_)) {
        ServerLog::error("FileTailer", "File not found: " + path_);
        return;
    }

    running_ = true;

    // Get initial file state - seek to end
    try {
        auto status = std::filesystem::status(path_);
        last_size_ = std::filesystem::file_size(path_);
        last_write_time_ = std::filesystem::last_write_time(path_);
        last_pos_ = static_cast<std::streampos>(last_size_);  // Start at end
    } catch (const std::exception& e) {
        ServerLog::error("FileTailer", std::string("Failed to stat file: ") + e.what());
        running_ = false;
        return;
    }

    ServerLog::log("FileTailer", "Started tailing: " + path_ + " (as " + source_name_ + ")");

    thread_ = std::thread([this]() {
        monitor_loop();
    });
}

void FileTailer::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
    ServerLog::log("FileTailer", "Stopped tailing: " + path_);
}

void FileTailer::monitor_loop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));

        if (!running_) break;

        try {
            // Check if file still exists
            if (!std::filesystem::exists(path_)) {
                // File was deleted - wait for it to reappear
                std::this_thread::sleep_for(std::chrono::seconds(1));
                continue;
            }

            auto current_size = std::filesystem::file_size(path_);

            // Handle file rotation (size decreased)
            if (current_size < static_cast<std::uintmax_t>(last_pos_)) {
                ServerLog::log("FileTailer", "File rotated, resetting position: " + path_);
                last_pos_ = 0;
            }

            // Only read if there's new content
            if (current_size > static_cast<std::uintmax_t>(last_pos_)) {
                std::ifstream file(path_);
                if (!file.is_open()) {
                    continue;
                }

                file.seekg(last_pos_);

                std::string line;
                while (std::getline(file, line) && running_) {
                    if (line.empty()) continue;

                    LogEntry entry;
                    entry.source = "file-tailer";
                    entry.category = source_name_;
                    entry.verbosity = Verbosity::Log;
                    entry.message = line;

                    auto now = std::chrono::system_clock::now();
                    entry.timestamp = std::chrono::duration<double>(now.time_since_epoch()).count();
                    entry.received_at = entry.timestamp;

                    store_.insert(entry);
                }

                last_pos_ = file.tellg();
                if (last_pos_ == std::streampos(-1)) {
                    // EOF reached, set to current size
                    last_pos_ = static_cast<std::streampos>(current_size);
                }
            }

            last_size_ = current_size;
        } catch (const std::exception& e) {
            ServerLog::error("FileTailer", std::string("Error reading file: ") + e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

} // namespace mcp_logs
