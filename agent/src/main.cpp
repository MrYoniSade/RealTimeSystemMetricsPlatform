#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <thread>
#include <vector>

#include "agent_config.h"
#include "structured_logger.h"
#include "metrics_collector.h"
#include "http_client.h"

std::atomic<bool> should_exit(false);

void signal_handler(int signum) {
    (void)signum;
    should_exit = true;
}

namespace {
std::vector<std::string> split_csv(const std::string& input) {
    std::vector<std::string> tokens;
    std::stringstream stream(input);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (!token.empty()) {
            tokens.push_back(token);
        }
    }
    return tokens;
}

bool apply_metrics_override(const std::string& csv, MetricsSelection& selection, std::string& error) {
    MetricsSelection updated{};
    updated.total_cpu = false;
    updated.per_core_cpu = false;
    updated.system_memory = false;
    updated.top_processes = false;
    updated.process_threads = false;
    updated.process_io = false;
    updated.process_handles = false;

    for (const std::string& raw_token : split_csv(csv)) {
        std::string token = raw_token;
        for (char& c : token) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }

        if (token == "all") {
            updated = MetricsSelection{};
            selection = updated;
            return true;
        }
        if (token == "total_cpu") {
            updated.total_cpu = true;
        } else if (token == "per_core_cpu") {
            updated.per_core_cpu = true;
        } else if (token == "system_memory") {
            updated.system_memory = true;
        } else if (token == "top_processes") {
            updated.top_processes = true;
        } else if (token == "process_threads") {
            updated.process_threads = true;
            updated.top_processes = true;
        } else if (token == "process_io") {
            updated.process_io = true;
            updated.top_processes = true;
        } else if (token == "process_handles") {
            updated.process_handles = true;
            updated.top_processes = true;
        } else {
            error = "Unknown metric selector: " + raw_token;
            return false;
        }
    }

    selection = updated;
    if (!selection.top_processes) {
        selection.process_threads = false;
        selection.process_io = false;
        selection.process_handles = false;
    }
    return true;
}
}

int main(int argc, char* argv[]) {
    AgentConfig config = AgentConfig::defaults();
    if (const char* backend_env = std::getenv("BACKEND_URL")) {
        config.backend_url = backend_env;
    }

    std::string config_path;
    if (const char* config_env = std::getenv("AGENT_CONFIG")) {
        config_path = config_env;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--config" && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    if (!config_path.empty()) {
        std::string error;
        if (!load_agent_config_file(config_path, config, error)) {
            log_event("ERROR", "config.load_failed", error, {{"path", config_path}});
            return 1;
        }
        log_event("INFO", "config.loaded", "Loaded runtime configuration", {{"path", config_path}});
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--backend-url" && i + 1 < argc) {
            config.backend_url = argv[++i];
        } else if (arg == "--interval" && i + 1 < argc) {
            config.interval_seconds = std::stoi(argv[++i]);
        } else if (arg == "--no-backend") {
            config.backend_enabled = false;
        } else if (arg == "--metrics" && i + 1 < argc) {
            std::string error;
            if (!apply_metrics_override(argv[++i], config.selection, error)) {
                log_event("ERROR", "config.invalid_metrics", error);
                return 1;
            }
        } else if (arg == "--config") {
            ++i;
        }
    }

    if (config.interval_seconds <= 0) {
        log_event("ERROR", "config.invalid_interval", "interval must be > 0");
        return 1;
    }

    if (config.queue_capacity == 0) {
        log_event("ERROR", "config.invalid_queue_capacity", "queue_capacity must be > 0");
        return 1;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    MetricsCollector collector(config.selection);
    std::unique_ptr<HttpClient> client;
    if (config.backend_enabled) {
        client = std::make_unique<HttpClient>(config.backend_url);
    }

    std::map<std::string, std::string> startup_fields = {
        {"backend_enabled", config.backend_enabled ? "true" : "false"},
        {"backend_url", config.backend_url},
        {"interval_seconds", std::to_string(config.interval_seconds)},
        {"queue_capacity", std::to_string(config.queue_capacity)}
    };
    log_event("INFO", "agent.start", "Metrics agent started", startup_fields);

    std::deque<SystemMetrics> queue;
    std::mutex queue_mutex;
    std::condition_variable queue_cv;

    std::thread collector_thread([&]() {
        while (!should_exit) {
            try {
                SystemMetrics metrics = collector.collect();

                size_t queue_size = 0;
                bool dropped_oldest = false;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex);
                    if (queue.size() >= config.queue_capacity) {
                        queue.pop_front();
                        dropped_oldest = true;
                    }
                    queue.push_back(std::move(metrics));
                    queue_size = queue.size();
                }
                queue_cv.notify_one();

                if (dropped_oldest) {
                    log_event("WARN", "collector.queue_overflow", "Dropped oldest metrics snapshot", {
                        {"queue_capacity", std::to_string(config.queue_capacity)}
                    });
                }

                log_event("INFO", "collector.snapshot", "Collected metrics snapshot", {
                    {"queue_size", std::to_string(queue_size)}
                });
            } catch (const std::exception& ex) {
                log_event("ERROR", "collector.error", "Collector failed", {{"error", ex.what()}});
            }

            const auto sleep_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(config.interval_seconds);
            while (!should_exit && std::chrono::steady_clock::now() < sleep_deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        queue_cv.notify_all();
    });

    std::thread sender_thread([&]() {
        while (true) {
            SystemMetrics metrics;
            {
                std::unique_lock<std::mutex> lock(queue_mutex);
                queue_cv.wait(lock, [&]() {
                    return should_exit || !queue.empty();
                });

                if (queue.empty() && should_exit) {
                    break;
                }
                if (queue.empty()) {
                    continue;
                }

                metrics = std::move(queue.front());
                queue.pop_front();
            }

            if (!config.backend_enabled) {
                log_event("INFO", "sender.skipped", "Backend disabled; metrics not sent", {
                    {"timestamp", std::to_string(metrics.timestamp)}
                });
                continue;
            }

            if (client->send_metrics(metrics)) {
                log_event("INFO", "sender.sent", "Sent metrics to backend", {
                    {"timestamp", std::to_string(metrics.timestamp)},
                    {"http_status", std::to_string(client->last_http_status())}
                });
            } else {
                log_event("ERROR", "sender.failed", "Failed to send metrics", {
                    {"timestamp", std::to_string(metrics.timestamp)},
                    {"error", client->last_error()},
                    {"http_status", std::to_string(client->last_http_status())}
                });
            }
        }
    });

    while (!should_exit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    queue_cv.notify_all();

    if (collector_thread.joinable()) {
        collector_thread.join();
    }
    if (sender_thread.joinable()) {
        sender_thread.join();
    }

    log_event("INFO", "agent.stop", "Metrics agent exited cleanly");
    return 0;
}
