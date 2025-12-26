#pragma once

#include "log_entry.hpp"
#include "log_store.hpp"
#include "server_log.hpp"
#include <string>
#include <thread>
#include <atomic>
#include <fstream>
#include <filesystem>

namespace mcp_logs {

class FileTailer {
public:
    FileTailer(LogStore& store, const std::string& path, const std::string& source_name = "");
    ~FileTailer();

    void start();
    void stop();
    bool is_running() const { return running_; }
    const std::string& path() const { return path_; }
    const std::string& source_name() const { return source_name_; }

private:
    void monitor_loop();
    std::string extract_filename(const std::string& path) const;

    LogStore& store_;
    std::string path_;
    std::string source_name_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::streampos last_pos_{0};
    std::uintmax_t last_size_{0};
    std::filesystem::file_time_type last_write_time_{};
};

} // namespace mcp_logs
