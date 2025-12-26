#pragma once

#include "log_store.hpp"
#include "file_tailer.hpp"
#include <string>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace mcp_logs {

struct SourceInfo {
    std::string id;
    std::string type;        // "file-tailer"
    std::string name;        // Display name
    std::string path;        // File path
    bool running;

    nlohmann::json to_json() const {
        return {
            {"id", id},
            {"type", type},
            {"name", name},
            {"path", path},
            {"running", running}
        };
    }
};

class SourceManager {
public:
    explicit SourceManager(LogStore& store);
    ~SourceManager();

    // File tailing
    std::string add_file_tailer(const std::string& path, const std::string& name = "");
    bool remove_source(const std::string& id);
    std::vector<SourceInfo> list_sources() const;

    // Lifecycle
    void stop_all();

private:
    LogStore& store_;
    std::map<std::string, std::unique_ptr<FileTailer>> tailers_;
    mutable std::mutex mutex_;
    int next_id_{1};
};

} // namespace mcp_logs
