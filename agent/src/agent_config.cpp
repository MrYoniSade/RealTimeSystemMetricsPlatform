#include "agent_config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <sstream>

namespace {
std::string trim(const std::string& value) {
    const auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    const auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    if (begin >= end) {
        return std::string();
    }
    return std::string(begin, end);
}

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool parse_bool_text(const std::string& value, bool& parsed) {
    const std::string normalized = to_lower(trim(value));
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
        parsed = true;
        return true;
    }
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
        parsed = false;
        return true;
    }
    return false;
}

bool parse_int_text(const std::string& value, int& parsed) {
    try {
        parsed = std::stoi(trim(value));
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_size_text(const std::string& value, size_t& parsed) {
    try {
        const long long raw = std::stoll(trim(value));
        if (raw <= 0) {
            return false;
        }
        parsed = static_cast<size_t>(raw);
        return true;
    } catch (...) {
        return false;
    }
}

bool extract_json_string(const std::string& content, const std::string& key, std::string& out) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*\"([^\"]*)\"");
    std::smatch match;
    if (!std::regex_search(content, match, pattern) || match.size() < 2) {
        return false;
    }
    out = match[1].str();
    return true;
}

bool extract_json_scalar(const std::string& content, const std::string& key, std::string& out) {
    const std::regex pattern("\"" + key + "\"\\s*:\\s*([^,}\\n]+)");
    std::smatch match;
    if (!std::regex_search(content, match, pattern) || match.size() < 2) {
        return false;
    }
    out = trim(match[1].str());
    return true;
}

bool extract_yaml_scalar(const std::string& content, const std::string& key, std::string& out) {
    const std::regex pattern("(^|\\n)\\s*" + key + "\\s*:\\s*([^\\n#]+)");
    std::smatch match;
    if (!std::regex_search(content, match, pattern) || match.size() < 3) {
        return false;
    }
    out = trim(match[2].str());
    return true;
}

bool extract_value(const std::string& content, const std::string& key, std::string& out) {
    if (extract_json_string(content, key, out)) {
        return true;
    }
    if (extract_json_scalar(content, key, out)) {
        return true;
    }
    if (extract_yaml_scalar(content, key, out)) {
        return true;
    }
    return false;
}

void apply_bool(const std::string& content, const std::string& key, bool& target) {
    std::string raw;
    if (!extract_value(content, key, raw)) {
        return;
    }

    bool parsed = false;
    if (parse_bool_text(raw, parsed)) {
        target = parsed;
    }
}

void apply_int(const std::string& content, const std::string& key, int& target) {
    std::string raw;
    if (!extract_value(content, key, raw)) {
        return;
    }

    int parsed = 0;
    if (parse_int_text(raw, parsed) && parsed > 0) {
        target = parsed;
    }
}

void apply_size(const std::string& content, const std::string& key, size_t& target) {
    std::string raw;
    if (!extract_value(content, key, raw)) {
        return;
    }

    size_t parsed = 0;
    if (parse_size_text(raw, parsed)) {
        target = parsed;
    }
}

void apply_string(const std::string& content, const std::string& key, std::string& target) {
    std::string raw;
    if (!extract_value(content, key, raw)) {
        return;
    }

    const std::string value = trim(raw);
    if (!value.empty()) {
        target = value;
    }
}
}

AgentConfig AgentConfig::defaults() {
    return AgentConfig{};
}

bool load_agent_config_file(const std::string& path, AgentConfig& config, std::string& error_message) {
    std::ifstream file(path);
    if (!file.is_open()) {
        error_message = "Unable to open config file: " + path;
        return false;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    const std::string content = buffer.str();

    if (content.empty()) {
        error_message = "Config file is empty: " + path;
        return false;
    }

    apply_string(content, "backend_url", config.backend_url);
    apply_bool(content, "backend_enabled", config.backend_enabled);
    apply_int(content, "interval_seconds", config.interval_seconds);
    apply_size(content, "queue_capacity", config.queue_capacity);

    apply_bool(content, "total_cpu", config.selection.total_cpu);
    apply_bool(content, "per_core_cpu", config.selection.per_core_cpu);
    apply_bool(content, "system_memory", config.selection.system_memory);
    apply_bool(content, "top_processes", config.selection.top_processes);
    apply_bool(content, "process_threads", config.selection.process_threads);
    apply_bool(content, "process_io", config.selection.process_io);
    apply_bool(content, "process_handles", config.selection.process_handles);

    if (config.interval_seconds <= 0) {
        error_message = "interval_seconds must be greater than 0";
        return false;
    }

    if (config.queue_capacity == 0) {
        error_message = "queue_capacity must be greater than 0";
        return false;
    }

    return true;
}
