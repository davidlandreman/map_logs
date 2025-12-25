#include <catch2/catch_test_macros.hpp>
#include "log_store.hpp"
#include <filesystem>
#include <algorithm>

using namespace mcp_logs;

TEST_CASE("LogStore basic operations", "[store]") {
    // Use temp database
    std::string db_path = "/tmp/test_logs.db";
    std::filesystem::remove(db_path);

    LogStore store(db_path);

    SECTION("Insert and query logs") {
        LogEntry entry;
        entry.source = "client";
        entry.category = "LogTemp";
        entry.verbosity = Verbosity::Warning;
        entry.message = "Test warning message";
        entry.timestamp = 1000.0;
        entry.session_id = "test_session";
        entry.instance_id = "test_instance";

        int64_t id = store.insert(entry);
        REQUIRE(id > 0);

        LogFilter filter;
        auto logs = store.query(filter);
        REQUIRE(logs.size() == 1);
        REQUIRE(logs[0].source == "client");
        REQUIRE(logs[0].message == "Test warning message");
    }

    SECTION("Filter by source") {
        LogEntry client_entry;
        client_entry.source = "client";
        client_entry.category = "LogTemp";
        client_entry.message = "Client message";
        client_entry.timestamp = 1000.0;
        client_entry.session_id = "test_session";
        client_entry.instance_id = "client_instance";
        store.insert(client_entry);

        LogEntry server_entry;
        server_entry.source = "server";
        server_entry.category = "LogTemp";
        server_entry.message = "Server message";
        server_entry.timestamp = 1001.0;
        server_entry.session_id = "test_session";
        server_entry.instance_id = "server_instance";
        store.insert(server_entry);

        LogFilter filter;
        filter.source = "client";
        auto logs = store.query(filter);
        REQUIRE(logs.size() >= 1);
        for (const auto& log : logs) {
            REQUIRE(log.source == "client");
        }
    }

    SECTION("Full-text search") {
        LogEntry entry1;
        entry1.source = "client";
        entry1.category = "LogTemp";
        entry1.message = "Player spawned at location";
        entry1.timestamp = 2000.0;
        entry1.session_id = "test_session";
        entry1.instance_id = "test_instance";
        store.insert(entry1);

        LogEntry entry2;
        entry2.source = "client";
        entry2.category = "LogTemp";
        entry2.message = "Enemy destroyed";
        entry2.timestamp = 2001.0;
        entry2.session_id = "test_session";
        entry2.instance_id = "test_instance";
        store.insert(entry2);

        LogFilter filter;
        auto logs = store.search("Player", filter);
        REQUIRE(logs.size() >= 1);
        REQUIRE(logs[0].message.find("Player") != std::string::npos);
    }

    SECTION("Get statistics") {
        auto stats = store.get_stats();
        REQUIRE(stats.total_count >= 0);
    }

    // Cleanup
    std::filesystem::remove(db_path);
}

TEST_CASE("LogEntry JSON serialization", "[entry]") {
    LogEntry entry;
    entry.id = 42;
    entry.source = "server";
    entry.category = "LogNet";
    entry.verbosity = Verbosity::Error;
    entry.message = "Connection failed";
    entry.timestamp = 12345.678;
    entry.frame = 100;
    entry.file = "NetDriver.cpp";
    entry.line = 256;
    entry.session_id = "match_12345";
    entry.instance_id = "server_1735000000000_abcd";

    auto json = entry.to_json();
    REQUIRE(json["id"] == 42);
    REQUIRE(json["source"] == "server");
    REQUIRE(json["verbosity"] == "Error");
    REQUIRE(json["session_id"] == "match_12345");
    REQUIRE(json["instance_id"] == "server_1735000000000_abcd");

    auto parsed = LogEntry::from_json(json);
    REQUIRE(parsed.source == entry.source);
    REQUIRE(parsed.message == entry.message);
    REQUIRE(parsed.verbosity == entry.verbosity);
    REQUIRE(parsed.session_id == entry.session_id);
    REQUIRE(parsed.instance_id == entry.instance_id);
}

TEST_CASE("LogStore session operations", "[store][session]") {
    std::string db_path = "/tmp/test_logs_session.db";
    std::filesystem::remove(db_path);

    LogStore store(db_path);

    SECTION("Insert logs with session_id and instance_id") {
        LogEntry entry;
        entry.source = "client";
        entry.category = "LogTemp";
        entry.message = "Test message";
        entry.timestamp = 1000.0;
        entry.session_id = "session_123";
        entry.instance_id = "client_1735000000000_a1b2";

        int64_t id = store.insert(entry);
        REQUIRE(id > 0);

        LogFilter filter;
        filter.all_sessions = true;  // Need to see all to verify
        auto logs = store.query(filter);
        REQUIRE(logs.size() == 1);
        REQUIRE(logs[0].session_id == "session_123");
        REQUIRE(logs[0].instance_id == "client_1735000000000_a1b2");
    }

    SECTION("Filter by session_id") {
        // Insert logs from two different sessions
        LogEntry entry1;
        entry1.source = "client";
        entry1.category = "LogTemp";
        entry1.message = "Session 1 message";
        entry1.timestamp = 1000.0;
        entry1.session_id = "session_1";
        entry1.instance_id = "client_1735000000000_1111";
        store.insert(entry1);

        LogEntry entry2;
        entry2.source = "client";
        entry2.category = "LogTemp";
        entry2.message = "Session 2 message";
        entry2.timestamp = 2000.0;
        entry2.session_id = "session_2";
        entry2.instance_id = "client_1735100000000_2222";
        store.insert(entry2);

        // Query specific session
        LogFilter filter;
        filter.session_id = "session_1";
        auto logs = store.query(filter);
        REQUIRE(logs.size() == 1);
        REQUIRE(logs[0].message == "Session 1 message");
    }

    SECTION("Latest session default behavior") {
        // Insert logs from old session
        LogEntry old_entry;
        old_entry.source = "client";
        old_entry.category = "LogTemp";
        old_entry.message = "Old session";
        old_entry.timestamp = 1000.0;
        old_entry.session_id = "old_session";
        old_entry.instance_id = "client_old";
        store.insert(old_entry);

        // Insert logs from new session (more recent received_at)
        LogEntry new_entry;
        new_entry.source = "client";
        new_entry.category = "LogTemp";
        new_entry.message = "New session";
        new_entry.timestamp = 2000.0;
        new_entry.session_id = "new_session";
        new_entry.instance_id = "client_new";
        store.insert(new_entry);

        // Default query should only return latest session
        LogFilter filter;
        // all_sessions is false by default
        auto logs = store.query(filter);
        REQUIRE(logs.size() == 1);
        REQUIRE(logs[0].session_id == "new_session");
    }

    SECTION("all_sessions returns everything") {
        LogEntry entry1;
        entry1.source = "client";
        entry1.category = "LogTemp";
        entry1.message = "First session";
        entry1.timestamp = 1000.0;
        entry1.session_id = "first";
        entry1.instance_id = "client_first";
        store.insert(entry1);

        LogEntry entry2;
        entry2.source = "client";
        entry2.category = "LogTemp";
        entry2.message = "Second session";
        entry2.timestamp = 2000.0;
        entry2.session_id = "second";
        entry2.instance_id = "client_second";
        store.insert(entry2);

        LogFilter filter;
        filter.all_sessions = true;
        auto logs = store.query(filter);
        REQUIRE(logs.size() == 2);
    }

    SECTION("Filter by instance_id within session") {
        // Two instances in same session
        LogEntry entry1;
        entry1.source = "client";
        entry1.category = "LogTemp";
        entry1.message = "Client 1";
        entry1.timestamp = 1000.0;
        entry1.session_id = "shared_session";
        entry1.instance_id = "client1_instance";
        store.insert(entry1);

        LogEntry entry2;
        entry2.source = "client";
        entry2.category = "LogTemp";
        entry2.message = "Client 2";
        entry2.timestamp = 1001.0;
        entry2.session_id = "shared_session";
        entry2.instance_id = "client2_instance";
        store.insert(entry2);

        LogFilter filter;
        filter.session_id = "shared_session";
        filter.instance_id = "client1_instance";
        auto logs = store.query(filter);
        REQUIRE(logs.size() == 1);
        REQUIRE(logs[0].message == "Client 1");
    }

    SECTION("get_sessions returns session list") {
        LogEntry entry1;
        entry1.source = "client";
        entry1.category = "LogTemp";
        entry1.message = "Session A log 1";
        entry1.timestamp = 1000.0;
        entry1.session_id = "session_a";
        entry1.instance_id = "client_a";
        store.insert(entry1);

        LogEntry entry2;
        entry2.source = "client";
        entry2.category = "LogTemp";
        entry2.message = "Session A log 2";
        entry2.timestamp = 1001.0;
        entry2.session_id = "session_a";
        entry2.instance_id = "client_a";
        store.insert(entry2);

        LogEntry entry3;
        entry3.source = "server";
        entry3.category = "LogTemp";
        entry3.message = "Session B log";
        entry3.timestamp = 2000.0;
        entry3.session_id = "session_b";
        entry3.instance_id = "server_b";
        store.insert(entry3);

        auto sessions = store.get_sessions();
        REQUIRE(sessions.size() >= 2);

        // Find session_a
        auto it = std::find_if(sessions.begin(), sessions.end(),
            [](const SessionInfo& s) { return s.session_id == "session_a"; });
        REQUIRE(it != sessions.end());
        REQUIRE(it->log_count == 2);
    }

    SECTION("get_latest_session returns most recent") {
        LogEntry entry1;
        entry1.source = "client";
        entry1.category = "LogTemp";
        entry1.message = "Old";
        entry1.timestamp = 1000.0;
        entry1.session_id = "older_session";
        entry1.instance_id = "client_old";
        store.insert(entry1);

        LogEntry entry2;
        entry2.source = "client";
        entry2.category = "LogTemp";
        entry2.message = "New";
        entry2.timestamp = 2000.0;
        entry2.session_id = "newer_session";
        entry2.instance_id = "client_new";
        store.insert(entry2);

        auto latest = store.get_latest_session();
        REQUIRE(latest == "newer_session");
    }

    // Cleanup
    std::filesystem::remove(db_path);
}
