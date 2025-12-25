#include "log_store.hpp"
#include <stdexcept>
#include <sstream>
#include <chrono>

namespace mcp_logs {

LogStore::LogStore(const std::string& db_path) {
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        throw std::runtime_error("Failed to open database: " + err);
    }

    // Enable WAL mode for better concurrent access
    exec("PRAGMA journal_mode=WAL");
    exec("PRAGMA synchronous=NORMAL");

    init_schema();
}

LogStore::~LogStore() {
    if (db_) {
        sqlite3_close(db_);
    }
}

void LogStore::exec(const std::string& sql) {
    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string error_msg = err ? err : "Unknown error";
        sqlite3_free(err);
        throw std::runtime_error("SQL error: " + error_msg);
    }
}

void LogStore::init_schema() {
    exec(R"(
        CREATE TABLE IF NOT EXISTS logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            source TEXT NOT NULL,
            category TEXT NOT NULL,
            verbosity INTEGER NOT NULL,
            message TEXT NOT NULL,
            timestamp REAL NOT NULL,
            frame INTEGER,
            file TEXT,
            line INTEGER,
            received_at REAL NOT NULL,
            session_id TEXT NOT NULL,
            instance_id TEXT NOT NULL
        )
    )");

    exec("CREATE INDEX IF NOT EXISTS idx_logs_source ON logs(source)");
    exec("CREATE INDEX IF NOT EXISTS idx_logs_verbosity ON logs(verbosity)");
    exec("CREATE INDEX IF NOT EXISTS idx_logs_timestamp ON logs(timestamp)");
    exec("CREATE INDEX IF NOT EXISTS idx_logs_category ON logs(category)");
    exec("CREATE INDEX IF NOT EXISTS idx_logs_received ON logs(received_at)");
    exec("CREATE INDEX IF NOT EXISTS idx_logs_session ON logs(session_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_logs_instance ON logs(instance_id)");
    exec("CREATE INDEX IF NOT EXISTS idx_logs_session_instance ON logs(session_id, instance_id)");

    // FTS5 virtual table for full-text search
    exec(R"(
        CREATE VIRTUAL TABLE IF NOT EXISTS logs_fts USING fts5(
            message,
            content='logs',
            content_rowid='id'
        )
    )");

    // Triggers to keep FTS in sync
    exec(R"(
        CREATE TRIGGER IF NOT EXISTS logs_ai AFTER INSERT ON logs BEGIN
            INSERT INTO logs_fts(rowid, message) VALUES (new.id, new.message);
        END
    )");

    exec(R"(
        CREATE TRIGGER IF NOT EXISTS logs_ad AFTER DELETE ON logs BEGIN
            INSERT INTO logs_fts(logs_fts, rowid, message) VALUES('delete', old.id, old.message);
        END
    )");

    exec(R"(
        CREATE TRIGGER IF NOT EXISTS logs_au AFTER UPDATE ON logs BEGIN
            INSERT INTO logs_fts(logs_fts, rowid, message) VALUES('delete', old.id, old.message);
            INSERT INTO logs_fts(rowid, message) VALUES (new.id, new.message);
        END
    )");
}

int64_t LogStore::insert(const LogEntry& entry) {
    std::lock_guard<std::mutex> lock(mutex_);

    const char* sql = R"(
        INSERT INTO logs (source, category, verbosity, message, timestamp, frame, file, line, received_at, session_id, instance_id)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare insert: " + std::string(sqlite3_errmsg(db_)));
    }

    double received_at = entry.received_at;
    if (received_at == 0.0) {
        auto now = std::chrono::system_clock::now();
        received_at = std::chrono::duration<double>(now.time_since_epoch()).count();
    }

    sqlite3_bind_text(stmt, 1, entry.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, entry.category.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, static_cast<int>(entry.verbosity));
    sqlite3_bind_text(stmt, 4, entry.message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, entry.timestamp);

    if (entry.frame) {
        sqlite3_bind_int64(stmt, 6, *entry.frame);
    } else {
        sqlite3_bind_null(stmt, 6);
    }

    if (entry.file) {
        sqlite3_bind_text(stmt, 7, entry.file->c_str(), -1, SQLITE_TRANSIENT);
    } else {
        sqlite3_bind_null(stmt, 7);
    }

    if (entry.line) {
        sqlite3_bind_int(stmt, 8, *entry.line);
    } else {
        sqlite3_bind_null(stmt, 8);
    }

    sqlite3_bind_double(stmt, 9, received_at);
    sqlite3_bind_text(stmt, 10, entry.session_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 11, entry.instance_id.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        throw std::runtime_error("Failed to insert log: " + std::string(sqlite3_errmsg(db_)));
    }

    int64_t id = sqlite3_last_insert_rowid(db_);

    // Create a copy with the ID for subscribers
    LogEntry inserted_entry = entry;
    inserted_entry.id = id;
    inserted_entry.received_at = received_at;

    // Notify subscribers (outside the lock would be better, but keeping simple)
    for (auto& callback : subscribers_) {
        callback(inserted_entry);
    }

    return id;
}

LogEntry LogStore::row_to_entry(sqlite3_stmt* stmt) {
    LogEntry entry;
    entry.id = sqlite3_column_int64(stmt, 0);
    entry.source = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    entry.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    entry.verbosity = static_cast<Verbosity>(sqlite3_column_int(stmt, 3));
    entry.message = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    entry.timestamp = sqlite3_column_double(stmt, 5);

    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
        entry.frame = sqlite3_column_int64(stmt, 6);
    }
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) {
        entry.file = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    }
    if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) {
        entry.line = sqlite3_column_int(stmt, 8);
    }
    entry.received_at = sqlite3_column_double(stmt, 9);
    entry.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 10));
    entry.instance_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 11));

    return entry;
}

std::vector<LogEntry> LogStore::query(const LogFilter& filter) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream sql;
    sql << "SELECT id, source, category, verbosity, message, timestamp, frame, file, line, received_at, session_id, instance_id FROM logs WHERE 1=1";

    std::vector<std::pair<int, std::string>> text_bindings;
    std::vector<std::pair<int, double>> double_bindings;
    std::vector<std::pair<int, int>> int_bindings;
    int param_idx = 1;

    // Session filtering: if not all_sessions, filter to latest session by default
    if (filter.session_id) {
        sql << " AND session_id = ?" << param_idx;
        text_bindings.emplace_back(param_idx++, *filter.session_id);
    } else if (!filter.all_sessions) {
        // Filter to latest session (subquery to find most recent session_id)
        sql << " AND session_id = (SELECT session_id FROM logs ORDER BY received_at DESC LIMIT 1)";
    }

    if (filter.instance_id) {
        sql << " AND instance_id = ?" << param_idx;
        text_bindings.emplace_back(param_idx++, *filter.instance_id);
    }

    if (filter.source) {
        sql << " AND source = ?" << param_idx;
        text_bindings.emplace_back(param_idx++, *filter.source);
    }

    if (filter.min_verbosity) {
        // Lower verbosity number = more severe (Fatal=1, Error=2, etc.)
        sql << " AND verbosity <= ?" << param_idx;
        int_bindings.emplace_back(param_idx++, static_cast<int>(*filter.min_verbosity));
    }

    if (filter.category) {
        sql << " AND category = ?" << param_idx;
        text_bindings.emplace_back(param_idx++, *filter.category);
    }

    if (filter.since) {
        sql << " AND timestamp >= ?" << param_idx;
        double_bindings.emplace_back(param_idx++, *filter.since);
    }

    if (filter.until) {
        sql << " AND timestamp <= ?" << param_idx;
        double_bindings.emplace_back(param_idx++, *filter.until);
    }

    sql << " ORDER BY timestamp DESC LIMIT " << filter.limit << " OFFSET " << filter.offset;

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare query: " + std::string(sqlite3_errmsg(db_)));
    }

    for (auto& [idx, val] : text_bindings) {
        sqlite3_bind_text(stmt, idx, val.c_str(), -1, SQLITE_TRANSIENT);
    }
    for (auto& [idx, val] : double_bindings) {
        sqlite3_bind_double(stmt, idx, val);
    }
    for (auto& [idx, val] : int_bindings) {
        sqlite3_bind_int(stmt, idx, val);
    }

    std::vector<LogEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(row_to_entry(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

std::vector<LogEntry> LogStore::search(const std::string& query, const LogFilter& filter) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream sql;
    sql << R"(
        SELECT l.id, l.source, l.category, l.verbosity, l.message, l.timestamp, l.frame, l.file, l.line, l.received_at, l.session_id, l.instance_id
        FROM logs l
        JOIN logs_fts fts ON l.id = fts.rowid
        WHERE logs_fts MATCH ?
    )";

    int param_idx = 2;

    // Session filtering: if not all_sessions, filter to latest session by default
    if (filter.session_id) {
        sql << " AND l.session_id = ?" << param_idx++;
    } else if (!filter.all_sessions) {
        sql << " AND l.session_id = (SELECT session_id FROM logs ORDER BY received_at DESC LIMIT 1)";
    }

    if (filter.instance_id) {
        sql << " AND l.instance_id = ?" << param_idx++;
    }

    if (filter.source) {
        sql << " AND l.source = ?" << param_idx++;
    }

    if (filter.min_verbosity) {
        sql << " AND l.verbosity <= ?" << param_idx++;
    }

    if (filter.since) {
        sql << " AND l.timestamp >= ?" << param_idx++;
    }

    if (filter.until) {
        sql << " AND l.timestamp <= ?" << param_idx++;
    }

    sql << " ORDER BY l.timestamp DESC LIMIT " << filter.limit << " OFFSET " << filter.offset;

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare search: " + std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_TRANSIENT);

    param_idx = 2;
    if (filter.session_id) {
        sqlite3_bind_text(stmt, param_idx++, filter.session_id->c_str(), -1, SQLITE_TRANSIENT);
    }
    if (filter.instance_id) {
        sqlite3_bind_text(stmt, param_idx++, filter.instance_id->c_str(), -1, SQLITE_TRANSIENT);
    }
    if (filter.source) {
        sqlite3_bind_text(stmt, param_idx++, filter.source->c_str(), -1, SQLITE_TRANSIENT);
    }
    if (filter.min_verbosity) {
        sqlite3_bind_int(stmt, param_idx++, static_cast<int>(*filter.min_verbosity));
    }
    if (filter.since) {
        sqlite3_bind_double(stmt, param_idx++, *filter.since);
    }
    if (filter.until) {
        sqlite3_bind_double(stmt, param_idx++, *filter.until);
    }

    std::vector<LogEntry> results;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        results.push_back(row_to_entry(stmt));
    }

    sqlite3_finalize(stmt);
    return results;
}

LogStats LogStore::get_stats(std::optional<std::string> source, std::optional<double> since) {
    std::lock_guard<std::mutex> lock(mutex_);

    LogStats stats;

    std::string where_clause = "WHERE 1=1";
    if (source) where_clause += " AND source = ?";
    if (since) where_clause += " AND timestamp >= ?";

    // Total count
    {
        std::string sql = "SELECT COUNT(*) FROM logs " + where_clause;
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

        int idx = 1;
        if (source) sqlite3_bind_text(stmt, idx++, source->c_str(), -1, SQLITE_TRANSIENT);
        if (since) sqlite3_bind_double(stmt, idx++, *since);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.total_count = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // Count by source
    {
        std::string sql = "SELECT source, COUNT(*) FROM logs " + where_clause + " GROUP BY source";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

        int idx = 1;
        if (source) sqlite3_bind_text(stmt, idx++, source->c_str(), -1, SQLITE_TRANSIENT);
        if (since) sqlite3_bind_double(stmt, idx++, *since);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string src = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int64_t count = sqlite3_column_int64(stmt, 1);
            if (src == "client") stats.client_count = count;
            else if (src == "server") stats.server_count = count;
        }
        sqlite3_finalize(stmt);
    }

    // Error count (Fatal + Error)
    {
        std::string sql = "SELECT COUNT(*) FROM logs " + where_clause + " AND verbosity <= 2";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

        int idx = 1;
        if (source) sqlite3_bind_text(stmt, idx++, source->c_str(), -1, SQLITE_TRANSIENT);
        if (since) sqlite3_bind_double(stmt, idx++, *since);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.error_count = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // Warning count
    {
        std::string sql = "SELECT COUNT(*) FROM logs " + where_clause + " AND verbosity = 3";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

        int idx = 1;
        if (source) sqlite3_bind_text(stmt, idx++, source->c_str(), -1, SQLITE_TRANSIENT);
        if (since) sqlite3_bind_double(stmt, idx++, *since);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.warning_count = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // By category
    {
        std::string sql = "SELECT category, COUNT(*) FROM logs " + where_clause + " GROUP BY category ORDER BY COUNT(*) DESC LIMIT 20";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

        int idx = 1;
        if (source) sqlite3_bind_text(stmt, idx++, source->c_str(), -1, SQLITE_TRANSIENT);
        if (since) sqlite3_bind_double(stmt, idx++, *since);

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            int64_t count = sqlite3_column_int64(stmt, 1);
            stats.by_category[cat] = count;
        }
        sqlite3_finalize(stmt);
    }

    // Session count
    {
        std::string sql = "SELECT COUNT(DISTINCT session_id) FROM logs " + where_clause;
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

        int idx = 1;
        if (source) sqlite3_bind_text(stmt, idx++, source->c_str(), -1, SQLITE_TRANSIENT);
        if (since) sqlite3_bind_double(stmt, idx++, *since);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.session_count = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // Instance count
    {
        std::string sql = "SELECT COUNT(DISTINCT instance_id) FROM logs " + where_clause;
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

        int idx = 1;
        if (source) sqlite3_bind_text(stmt, idx++, source->c_str(), -1, SQLITE_TRANSIENT);
        if (since) sqlite3_bind_double(stmt, idx++, *since);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            stats.instance_count = sqlite3_column_int64(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }

    // Current session (most recent)
    {
        std::string sql = "SELECT session_id FROM logs ORDER BY received_at DESC LIMIT 1";
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* session = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (session) stats.current_session = session;
        }
        sqlite3_finalize(stmt);
    }

    return stats;
}

std::vector<std::string> LogStore::get_categories(std::optional<std::string> source) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string sql = "SELECT DISTINCT category FROM logs";
    if (source) sql += " WHERE source = ?";
    sql += " ORDER BY category";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);

    if (source) {
        sqlite3_bind_text(stmt, 1, source->c_str(), -1, SQLITE_TRANSIENT);
    }

    std::vector<std::string> categories;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        categories.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
    }

    sqlite3_finalize(stmt);
    return categories;
}

int64_t LogStore::clear(std::optional<std::string> source, std::optional<double> before) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream sql;
    sql << "DELETE FROM logs WHERE 1=1";

    if (source) sql << " AND source = ?";
    if (before) sql << " AND timestamp < ?";

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr);

    int idx = 1;
    if (source) sqlite3_bind_text(stmt, idx++, source->c_str(), -1, SQLITE_TRANSIENT);
    if (before) sqlite3_bind_double(stmt, idx++, *before);

    sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    return sqlite3_changes(db_);
}

int64_t LogStore::count() {
    std::lock_guard<std::mutex> lock(mutex_);

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM logs", -1, &stmt, nullptr);

    int64_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int64(stmt, 0);
    }

    sqlite3_finalize(stmt);
    return count;
}

void LogStore::subscribe(LogCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    subscribers_.push_back(std::move(callback));
}

std::vector<SessionInfo> LogStore::get_sessions(std::optional<std::string> source) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream sql;
    sql << R"(
        SELECT session_id,
               MIN(received_at) as first_seen,
               MAX(received_at) as last_seen,
               COUNT(*) as log_count
        FROM logs
    )";

    if (source) {
        sql << " WHERE source = ?";
    }

    sql << " GROUP BY session_id ORDER BY last_seen DESC";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.str().c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare get_sessions: " + std::string(sqlite3_errmsg(db_)));
    }

    if (source) {
        sqlite3_bind_text(stmt, 1, source->c_str(), -1, SQLITE_TRANSIENT);
    }

    std::vector<SessionInfo> sessions;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SessionInfo info;
        info.session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        info.first_seen = sqlite3_column_double(stmt, 1);
        info.last_seen = sqlite3_column_double(stmt, 2);
        info.log_count = sqlite3_column_int64(stmt, 3);
        sessions.push_back(info);
    }
    sqlite3_finalize(stmt);

    // Get instances for each session
    for (auto& session : sessions) {
        std::ostringstream inst_sql;
        inst_sql << "SELECT DISTINCT instance_id FROM logs WHERE session_id = ?";
        if (source) {
            inst_sql << " AND source = ?";
        }

        sqlite3_stmt* inst_stmt;
        sqlite3_prepare_v2(db_, inst_sql.str().c_str(), -1, &inst_stmt, nullptr);
        sqlite3_bind_text(inst_stmt, 1, session.session_id.c_str(), -1, SQLITE_TRANSIENT);
        if (source) {
            sqlite3_bind_text(inst_stmt, 2, source->c_str(), -1, SQLITE_TRANSIENT);
        }

        while (sqlite3_step(inst_stmt) == SQLITE_ROW) {
            session.instances.emplace_back(reinterpret_cast<const char*>(sqlite3_column_text(inst_stmt, 0)));
        }
        sqlite3_finalize(inst_stmt);
    }

    return sessions;
}

std::string LogStore::get_latest_session(std::optional<std::string> source) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string sql = "SELECT session_id FROM logs";
    if (source) {
        sql += " WHERE source = ?";
    }
    sql += " ORDER BY received_at DESC LIMIT 1";

    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Failed to prepare get_latest_session: " + std::string(sqlite3_errmsg(db_)));
    }

    if (source) {
        sqlite3_bind_text(stmt, 1, source->c_str(), -1, SQLITE_TRANSIENT);
    }

    std::string result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* session = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (session) result = session;
    }

    sqlite3_finalize(stmt);
    return result;
}

} // namespace mcp_logs
