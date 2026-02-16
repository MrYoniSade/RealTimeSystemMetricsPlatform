#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <cstdlib>
#include <memory>
#include "metrics_collector.h"
#include "http_client.h"

std::atomic<bool> should_exit(false);

void signal_handler(int signum) {
    std::cout << "\nShutdown signal received. Gracefully exiting...\n";
    should_exit = true;
}

int main(int argc, char* argv[]) {
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    std::string backend_url = "http://localhost:8000";
    int interval_seconds = 2;
    bool backend_enabled = true;

    if (const char* backend_env = std::getenv("BACKEND_URL")) {
        backend_url = backend_env;
    }
    
    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--backend-url" && i + 1 < argc) {
            backend_url = argv[++i];
        } else if (arg == "--interval" && i + 1 < argc) {
            interval_seconds = std::stoi(argv[++i]);
        } else if (arg == "--no-backend") {
            backend_enabled = false;
        }
    }
    
    // Register signal handler
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    MetricsCollector collector;
    std::unique_ptr<HttpClient> client;
    if (backend_enabled) {
        client = std::make_unique<HttpClient>(backend_url);
    }
    
    std::cout << "Metrics Agent started\n";
    if (backend_enabled) {
        std::cout << "Backend URL: " << backend_url << "\n";
    } else {
        std::cout << "Backend: disabled (--no-backend)\n";
    }
    std::cout << "Collection interval: " << interval_seconds << " seconds\n";
    if (backend_enabled && backend_url.find("://backend") != std::string::npos) {
        std::cout << "Hint: 'backend' is a container/Kubernetes hostname. For local runs use --backend-url http://localhost:8000\n";
    }
    std::cout << "Press Ctrl+C to exit\n\n";
    
    while (!should_exit) {
        try {
            SystemMetrics metrics = collector.collect();
            
            std::cout << "Collected metrics at " << metrics.timestamp 
                      << " (CPU: " << metrics.total_cpu_percent << "%)\n";

            if (!backend_enabled) {
                std::cout << "  -> Backend disabled, skipping send\n";
            } else {
                if (client->send_metrics(metrics)) {
                    std::cout << "  -> Sent to backend successfully\n";
                } else {
                    std::cerr << "  -> Failed to send metrics to backend\n";
                    if (!client->last_error().empty()) {
                        std::cerr << "     Reason: " << client->last_error() << "\n";
                    }
                }
            }
            
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
        }
        
        // Sleep for the specified interval
        for (int i = 0; i < interval_seconds && !should_exit; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
    
    std::cout << "Metrics Agent exited cleanly\n";
    return 0;
}
