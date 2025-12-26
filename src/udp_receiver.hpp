#pragma once

#include "log_entry.hpp"
#include "log_store.hpp"
#include "server_log.hpp"
#include <asio.hpp>
#include <thread>
#include <atomic>
#include <functional>

namespace mcp_logs {

class UdpReceiver {
public:
    UdpReceiver(LogStore& store, uint16_t port = 52099)
        : store_(store)
        , socket_(io_context_, asio::ip::udp::endpoint(asio::ip::udp::v4(), port))
        , running_(false)
    {
        ServerLog::log("UDP", "Listening on port " + std::to_string(port));
    }

    ~UdpReceiver() {
        stop();
    }

    void start() {
        if (running_) return;
        running_ = true;
        start_receive();
        thread_ = std::thread([this]() {
            io_context_.run();
        });
    }

    void stop() {
        if (!running_) return;
        running_ = false;
        io_context_.stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

private:
    void start_receive() {
        socket_.async_receive_from(
            asio::buffer(recv_buffer_), remote_endpoint_,
            [this](const asio::error_code& error, std::size_t bytes_received) {
                handle_receive(error, bytes_received);
            }
        );
    }

    void handle_receive(const asio::error_code& error, std::size_t bytes_received) {
        if (!running_) return;

        if (!error && bytes_received > 0) {
            try {
                std::string data(recv_buffer_.data(), bytes_received);
                auto json = nlohmann::json::parse(data);
                LogEntry entry = LogEntry::from_json(json);

                // Set received timestamp
                auto now = std::chrono::system_clock::now();
                entry.received_at = std::chrono::duration<double>(now.time_since_epoch()).count();

                store_.insert(entry);
            } catch (const std::exception& e) {
                ServerLog::error("UDP", std::string("Failed to parse log: ") + e.what());
            }
        }

        start_receive();
    }

    LogStore& store_;
    asio::io_context io_context_;
    asio::ip::udp::socket socket_;
    asio::ip::udp::endpoint remote_endpoint_;
    std::array<char, 65536> recv_buffer_;
    std::thread thread_;
    std::atomic<bool> running_;
};

} // namespace mcp_logs
