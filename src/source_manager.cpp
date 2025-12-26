#include "source_manager.hpp"

namespace mcp_logs {

SourceManager::SourceManager(LogStore& store)
    : store_(store)
{
}

SourceManager::~SourceManager() {
    stop_all();
}

std::string SourceManager::add_file_tailer(const std::string& path, const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string id = "file-" + std::to_string(next_id_++);

    auto tailer = std::make_unique<FileTailer>(store_, path, name);
    tailer->start();

    if (!tailer->is_running()) {
        // Failed to start (file not found, etc.)
        return "";
    }

    tailers_[id] = std::move(tailer);
    return id;
}

bool SourceManager::remove_source(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = tailers_.find(id);
    if (it == tailers_.end()) {
        return false;
    }

    it->second->stop();
    tailers_.erase(it);
    return true;
}

std::vector<SourceInfo> SourceManager::list_sources() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<SourceInfo> result;
    for (const auto& [id, tailer] : tailers_) {
        SourceInfo info;
        info.id = id;
        info.type = "file-tailer";
        info.name = tailer->source_name();
        info.path = tailer->path();
        info.running = tailer->is_running();
        result.push_back(info);
    }
    return result;
}

void SourceManager::stop_all() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [id, tailer] : tailers_) {
        tailer->stop();
    }
    tailers_.clear();
}

} // namespace mcp_logs
