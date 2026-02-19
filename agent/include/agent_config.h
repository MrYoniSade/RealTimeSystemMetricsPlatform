#pragma once

#include <cstddef>
#include <string>

#include "metrics_collector.h"

struct AgentConfig {
    std::string backend_url = "http://localhost:8000";
    int interval_seconds = 2;
    bool backend_enabled = true;
    size_t queue_capacity = 32;
    MetricsSelection selection{};

    static AgentConfig defaults();
};

bool load_agent_config_file(const std::string& path, AgentConfig& config, std::string& error_message);
