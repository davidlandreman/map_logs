#include "console_ui.hpp"
#include "source_manager.hpp"
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>
#include <ftxui/screen/terminal.hpp>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>

namespace mcp_logs {

// LogBuffer implementation
template<typename T>
LogBuffer<T>::LogBuffer(size_t max_lines) : max_lines_(max_lines) {}

template<typename T>
void LogBuffer<T>::push(T line) {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.push_back(std::move(line));
    while (lines_.size() > max_lines_) {
        lines_.pop_front();
    }
}

template<typename T>
std::vector<T> LogBuffer<T>::get_lines() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<T>(lines_.begin(), lines_.end());
}

template<typename T>
size_t LogBuffer<T>::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return lines_.size();
}

template<typename T>
void LogBuffer<T>::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    lines_.clear();
}

// Explicit template instantiations
template class LogBuffer<DisplayLogLine>;
template class LogBuffer<ServerLogLine>;

// ConsoleUI constructor
ConsoleUI::ConsoleUI(LogStore& store, SourceManager& sources, uint16_t udp_port, uint16_t http_port,
                     bool is_https, const std::string& db_path)
    : store_(store)
    , sources_(sources)
    , udp_logs_(1000)
    , server_logs_(500)
    , udp_port_(udp_port)
    , http_port_(http_port)
    , is_https_(is_https)
    , db_path_(db_path)
    , rate_window_start_(std::chrono::steady_clock::now())
{
    // Subscribe to LogStore for UDP logs
    store_.subscribe([this](const LogEntry& entry) {
        on_udp_log(entry);
    });
}

ConsoleUI::~ConsoleUI() = default;

void ConsoleUI::on_udp_log(const LogEntry& entry) {
    if (paused_) return;

    DisplayLogLine line;
    line.category = entry.category;
    line.message = entry.message;
    line.verbosity = entry.verbosity;
    line.received_at = std::chrono::steady_clock::now();

    udp_logs_.push(std::move(line));
    logs_in_window_++;

    // Trigger screen refresh if we have a screen
    if (screen_) {
        screen_->Post(ftxui::Event::Custom);
    }
}

void ConsoleUI::log_server(const std::string& component,
                            const std::string& message, bool is_error) {
    ServerLogLine line;
    line.component = component;
    line.message = message;
    line.is_error = is_error;
    line.timestamp = std::chrono::steady_clock::now();

    server_logs_.push(std::move(line));

    if (screen_) {
        screen_->Post(ftxui::Event::Custom);
    }
}

ConsoleUI::ServerLogSink ConsoleUI::get_log_sink() {
    return [this](const std::string& component, const std::string& msg, bool err) {
        this->log_server(component, msg, err);
    };
}

ftxui::Color ConsoleUI::verbosity_to_color(Verbosity v) {
    using namespace ftxui;
    switch (v) {
        case Verbosity::Fatal:
        case Verbosity::Error:
            return Color::Red;
        case Verbosity::Warning:
            return Color::Yellow;
        case Verbosity::Display:
        case Verbosity::Log:
            return Color::White;
        case Verbosity::Verbose:
        case Verbosity::VeryVerbose:
            return Color::GrayDark;
        default:
            return Color::White;
    }
}

void ConsoleUI::update_stats() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - rate_window_start_).count();

    if (elapsed >= 1) {
        double rate = static_cast<double>(logs_in_window_) / std::max(1LL, elapsed);
        logs_in_window_ = 0;
        rate_window_start_ = now;

        auto db_stats = store_.get_stats();

        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.total_logs = db_stats.total_count;
        stats_.error_count = db_stats.error_count;
        stats_.warning_count = db_stats.warning_count;
        stats_.session_count = db_stats.session_count;
        stats_.logs_per_second = rate;
        stats_.current_session = db_stats.current_session;
    }
}

void ConsoleUI::init_commands(std::atomic<bool>& running, ftxui::ScreenInteractive& screen) {
    commands_ = {
        {"quit", "Exit the application", [&running, &screen](ConsoleUI&, const std::vector<std::string>&) {
            running = false;
            screen.Exit();
        }, false},
        {"q", "Exit (alias for quit)", [&running, &screen](ConsoleUI&, const std::vector<std::string>&) {
            running = false;
            screen.Exit();
        }, false},
        {"pause", "Toggle log pause", [](ConsoleUI& ui, const std::vector<std::string>&) {
            ui.paused_ = !ui.paused_;
        }, false},
        {"p", "Toggle pause (alias)", [](ConsoleUI& ui, const std::vector<std::string>&) {
            ui.paused_ = !ui.paused_;
        }, false},
        {"clear", "Clear source log display", [](ConsoleUI& ui, const std::vector<std::string>&) {
            ui.udp_logs_.clear();
        }, false},
        {"delete-logs", "Delete all logs from database", [](ConsoleUI& ui, const std::vector<std::string>&) {
            int64_t count = ui.store_.clear();
            ui.udp_logs_.clear();
            ui.log_server("DB", "Deleted " + std::to_string(count) + " logs from database", false);
        }, false},
        {"tail", "Tail a file: /tail <path> [name]", [](ConsoleUI& ui, const std::vector<std::string>& args) {
            if (args.empty()) {
                ui.log_server("Tail", "Usage: /tail <path> [name]", true);
                return;
            }
            std::string path = args[0];
            std::string name = args.size() > 1 ? args[1] : "";
            auto id = ui.sources_.add_file_tailer(path, name);
            if (id.empty()) {
                ui.log_server("Tail", "Failed to tail file: " + path, true);
            } else {
                ui.log_server("Tail", "Started tailing " + path + " (id: " + id + ")", false);
            }
        }, true},
        {"untail", "Stop tailing: /untail <id>", [](ConsoleUI& ui, const std::vector<std::string>& args) {
            if (args.empty()) {
                ui.log_server("Untail", "Usage: /untail <id>", true);
                return;
            }
            if (ui.sources_.remove_source(args[0])) {
                ui.log_server("Untail", "Stopped source: " + args[0], false);
            } else {
                ui.log_server("Untail", "Source not found: " + args[0], true);
            }
        }, true},
        {"sources", "List active sources", [](ConsoleUI& ui, const std::vector<std::string>&) {
            auto sources = ui.sources_.list_sources();
            if (sources.empty()) {
                ui.log_server("Sources", "No active file sources", false);
            } else {
                ui.log_server("Sources", "Active sources:", false);
                for (const auto& src : sources) {
                    ui.log_server("Sources", "  " + src.id + ": " + src.name + " (" + src.path + ")" +
                        (src.running ? "" : " [stopped]"), false);
                }
            }
        }, false},
        {"help", "Show available commands", [](ConsoleUI& ui, const std::vector<std::string>&) {
            ui.log_server("Help", "Available commands:", false);
            ui.log_server("Help", "  /quit, /q        - Exit the application", false);
            ui.log_server("Help", "  /pause, /p       - Toggle log pause", false);
            ui.log_server("Help", "  /clear           - Clear source log display", false);
            ui.log_server("Help", "  /delete-logs     - Delete all logs from database", false);
            ui.log_server("Help", "  /tail <path>     - Start tailing a file", false);
            ui.log_server("Help", "  /untail <id>     - Stop tailing a source", false);
            ui.log_server("Help", "  /sources         - List active sources", false);
            ui.log_server("Help", "  /help, /h        - Show this help", false);
        }, false},
        {"h", "Help (alias)", [](ConsoleUI& ui, const std::vector<std::string>&) {
            ui.log_server("Help", "Available commands:", false);
            ui.log_server("Help", "  /quit, /q        - Exit the application", false);
            ui.log_server("Help", "  /pause, /p       - Toggle log pause", false);
            ui.log_server("Help", "  /clear           - Clear source log display", false);
            ui.log_server("Help", "  /delete-logs     - Delete all logs from database", false);
            ui.log_server("Help", "  /tail <path>     - Start tailing a file", false);
            ui.log_server("Help", "  /untail <id>     - Stop tailing a source", false);
            ui.log_server("Help", "  /sources         - List active sources", false);
            ui.log_server("Help", "  /help, /h        - Show this help", false);
        }, false},
    };
}

void ConsoleUI::execute_command(std::atomic<bool>& running, ftxui::ScreenInteractive& screen) {
    if (command_input_.empty()) return;

    std::string input = command_input_;
    command_input_.clear();
    completion_hint_.clear();

    // Remove leading / if present
    if (!input.empty() && input[0] == '/') {
        input = input.substr(1);
    }

    if (input.empty()) return;

    // Parse command and arguments
    std::istringstream iss(input);
    std::string cmd;
    iss >> cmd;

    std::vector<std::string> args;
    std::string arg;
    while (iss >> arg) {
        args.push_back(arg);
    }

    // Find and execute command
    for (const auto& command : commands_) {
        if (command.name == cmd) {
            command.handler(*this, args);
            return;
        }
    }

    // Unknown command
    log_server("Command", "Unknown command: /" + cmd + " (type /help for available commands)", true);
}

std::string ConsoleUI::complete_command(const std::string& partial) {
    if (partial.empty() || partial[0] != '/') return partial;

    std::string prefix = partial.substr(1); // Remove leading /
    std::vector<std::string> matches;

    for (const auto& cmd : commands_) {
        if (cmd.name.find(prefix) == 0) {
            matches.push_back(cmd.name);
        }
    }

    if (matches.size() == 1) {
        // Exact single match - complete it
        return "/" + matches[0];
    } else if (matches.size() > 1 && !prefix.empty()) {
        // Find common prefix among matches
        std::string common = matches[0];
        for (size_t i = 1; i < matches.size(); ++i) {
            size_t j = 0;
            while (j < common.size() && j < matches[i].size() &&
                   common[j] == matches[i][j]) {
                ++j;
            }
            common = common.substr(0, j);
        }
        if (common.size() > prefix.size()) {
            return "/" + common;
        }
    }
    return partial;
}

void ConsoleUI::update_completion_hint() {
    if (command_input_.empty()) {
        completion_hint_ = "Type /help for commands";
        return;
    }

    if (command_input_[0] != '/') {
        completion_hint_ = "Commands start with /";
        return;
    }

    if (command_input_.size() == 1) {
        // Just "/" - show all main commands
        completion_hint_ = "quit, pause, clear, tail, untail, sources, help";
        return;
    }

    std::string prefix = command_input_.substr(1);

    // Check for exact match
    for (const auto& cmd : commands_) {
        if (cmd.name == prefix) {
            completion_hint_ = "";
            return;
        }
    }

    // Find matching commands
    std::vector<std::string> matches;
    for (const auto& cmd : commands_) {
        if (cmd.name.find(prefix) == 0) {
            matches.push_back(cmd.name);
        }
    }

    if (matches.empty()) {
        completion_hint_ = "(no match)";
    } else if (matches.size() == 1) {
        completion_hint_ = "Tab: " + matches[0];
    } else {
        std::string hint = "Tab: ";
        for (size_t i = 0; i < matches.size(); ++i) {
            if (i > 0) hint += ", ";
            hint += matches[i];
        }
        completion_hint_ = hint;
    }
}

void ConsoleUI::handle_tab_completion() {
    if (command_input_.empty()) {
        command_input_ = "/";
        update_completion_hint();
        return;
    }

    std::string completed = complete_command(command_input_);
    if (completed != command_input_) {
        command_input_ = completed;
    }
    update_completion_hint();
}

void ConsoleUI::run(std::atomic<bool>& running) {
    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();
    screen_ = &screen;

    // Initialize commands
    init_commands(running, screen);
    update_completion_hint();

    // Stats update thread
    std::atomic<bool> stats_running{true};
    std::thread stats_thread([this, &stats_running]() {
        while (stats_running) {
            update_stats();
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (screen_) {
                screen_->Post(Event::Custom);
            }
        }
    });

    // Create Input component for command bar with custom styling
    auto input_option = InputOption::Default();
    input_option.transform = [](InputState state) {
        state.element |= color(Color::White);
        return state.element;
    };
    auto input_component = Input(&command_input_, "", input_option);

    // Wrap input to intercept Tab, Escape, and track changes
    auto command_input_handler = CatchEvent(input_component, [this, &running, &screen](Event event) {
        if (event == Event::Tab) {
            handle_tab_completion();
            return true;
        }
        if (event == Event::Escape) {
            command_input_.clear();
            update_completion_hint();
            return true;
        }
        if (event == Event::Return) {
            execute_command(running, screen);
            return true;
        }
        // Update hints after any character input
        if (event.is_character()) {
            // Let the input handle it first, then update hints
            return false;
        }
        return false;
    });

    // Wrap again to update hints after input changes
    auto command_with_hints = CatchEvent(command_input_handler, [this](Event event) {
        // After any event, update hints (deferred)
        if (event.is_character() || event == Event::Backspace || event == Event::Delete) {
            // This runs after the underlying input processes the character
            update_completion_hint();
        }
        return false;
    });

    // Main content renderer (log panes only, no command bar)
    auto main_content = Renderer([this]() {
        using namespace ftxui;

        // Get current stats
        DisplayStats current_stats;
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            current_stats = stats_;
        }

        // Top bar with ASCII logo
        std::string protocol = is_https_ ? "HTTPS" : "HTTP";

        // ASCII art logo (3 lines high) - stacked logs representation
        auto logo = vbox({
            text("┌──┐") | color(Color::Cyan),
            text("├──┤") | color(Color::Cyan),
            text("└──┘") | color(Color::Cyan),
        });

        // App info (right of logo)
        auto app_info = vbox({
            hbox({
                text(" Map Logs") | bold | color(Color::White),
                text(" v0.1.0") | dim,
            }),
            text(" Multisource log aggregation for agentic AI") | dim,
            text(" David Landreman") | dim | color(Color::GrayLight),
        });

        // Stats section (right side) - emojis instead of labels
        auto stats_line = hbox({
            text("📋 ") | dim,
            text(std::to_string(current_stats.total_logs)),
            text("  "),
            text("❌ ") | dim,
            text(std::to_string(current_stats.error_count)) | color(Color::Red),
            text("  "),
            text("⚠️  ") | dim,
            text(std::to_string(current_stats.warning_count)) | color(Color::Yellow),
            text("  "),
            text("⚡ ") | dim,
            text(std::to_string(static_cast<int>(current_stats.logs_per_second)) + "/s"),
        });

        auto info_line = hbox({
            text(protocol + ":" + std::to_string(http_port_)) | dim,
            text(" │ ") | dim,
            text("UDP:" + std::to_string(udp_port_)) | dim,
        });

        auto stats_box = vbox({
            stats_line,
            info_line,
            text("") | dim,  // Empty line for alignment
        });

        auto top_bar = hbox({
            text(" "),
            logo,
            app_info,
            filler(),
            stats_box,
            text(" "),
        });

        // Source logs pane
        auto udp_lines = udp_logs_.get_lines();
        Elements udp_elements;

        size_t start_idx = udp_lines.size() > 100 ? udp_lines.size() - 100 : 0;
        for (size_t i = start_idx; i < udp_lines.size(); ++i) {
            const auto& line = udp_lines[i];
            std::string prefix = "[" + line.category + "] ";
            auto elem = hbox({
                text(prefix) | dim,
                text(line.message) | color(verbosity_to_color(line.verbosity))
            });
            udp_elements.push_back(elem);
        }

        auto udp_pane = vbox({
            hbox({
                text(" Source Logs ") | bold,
                filler(),
                text("(" + std::to_string(udp_logs_.size()) + ")") | dim,
            }),
            separator() | color(Color::GrayDark),
            vbox(std::move(udp_elements)) | focusPositionRelative(0, 1) | vscroll_indicator | yframe | flex,
        }) | flex | border | color(Color::GrayDark);

        // Server logs pane
        auto server_lines = server_logs_.get_lines();
        Elements server_elements;

        size_t srv_start = server_lines.size() > 100 ? server_lines.size() - 100 : 0;
        for (size_t i = srv_start; i < server_lines.size(); ++i) {
            const auto& line = server_lines[i];
            std::string prefix = "[" + line.component + "] ";
            std::string full_msg = prefix + line.message;
            auto elem = paragraph(full_msg) | (line.is_error ? color(Color::Red) : nothing);
            server_elements.push_back(elem);
        }

        auto server_pane = vbox({
            hbox({
                text(" Server Logs ") | bold,
                filler(),
                text("(" + std::to_string(server_logs_.size()) + ")") | dim,
            }),
            separator() | color(Color::GrayDark),
            vbox(std::move(server_elements)) | focusPositionRelative(0, 1) | vscroll_indicator | yframe | flex,
        }) | flex | border | color(Color::GrayDark);

        // Main content area - split panes (2:1 ratio), full width
        auto content = hbox({
            udp_pane | flex,
            server_pane | size(WIDTH, EQUAL, 40),
        }) | flex;

        return vbox({
            text(""),  // Top spacing
            top_bar,
            content,
        });
    });

    // Command bar with input field and hints (no border)
    auto cmd_bar = Renderer(command_with_hints, [this, &input_component]() {
        return hbox({
            text(" > ") | bold | color(Color::GrayLight),
            input_component->Render() | size(WIDTH, GREATER_THAN, 20),
            filler(),
            paused_ ? (text(" PAUSED ") | bgcolor(Color::Yellow) | color(Color::Black)) : text(""),
            text(completion_hint_) | dim | color(Color::GrayDark),
            text(" "),
        });
    });

    // Combine main content and command bar with proper layout constraints
    auto main_layout = Renderer(command_with_hints, [&main_content, &cmd_bar]() {
        return vbox({
            main_content->Render() | flex,
            separator() | color(Color::GrayDark),
            cmd_bar->Render() | size(HEIGHT, EQUAL, 1),
            text(""),  // Bottom spacing
        });
    });

    // Run the main loop
    screen.Loop(main_layout);

    // Cleanup
    stats_running = false;
    stats_thread.join();
    screen_ = nullptr;
}

} // namespace mcp_logs
