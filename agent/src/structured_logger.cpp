#include "structured_logger.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
std::string escape_json_string(const std::string& input) {
    std::ostringstream escaped;
    for (const char character : input) {
        switch (character) {
            case '\\':
                escaped << "\\\\";
                break;
            case '"':
                escaped << "\\\"";
                break;
            case '\n':
                escaped << "\\n";
                break;
            case '\r':
                escaped << "\\r";
                break;
            case '\t':
                escaped << "\\t";
                break;
            default:
                escaped << character;
                break;
        }
    }
    return escaped.str();
}

std::string now_utc_iso8601() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t current_time = std::chrono::system_clock::to_time_t(now);

    std::tm time_info{};
#if defined(_WIN32)
    gmtime_s(&time_info, &current_time);
#else
    gmtime_r(&current_time, &time_info);
#endif

    std::ostringstream out;
    out << std::put_time(&time_info, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}
}

void log_event(
    const std::string& level,
    const std::string& event,
    const std::string& message,
    const std::map<std::string, std::string>& fields
) {
    std::ostringstream entry;
    entry << "{";
    entry << "\"ts\":\"" << escape_json_string(now_utc_iso8601()) << "\",";
    entry << "\"level\":\"" << escape_json_string(level) << "\",";
    entry << "\"event\":\"" << escape_json_string(event) << "\",";
    entry << "\"message\":\"" << escape_json_string(message) << "\"";

    for (const auto& [key, value] : fields) {
        entry << ",\"" << escape_json_string(key) << "\":\"" << escape_json_string(value) << "\"";
    }

    entry << "}";

    if (level == "ERROR" || level == "WARN") {
        std::cerr << entry.str() << std::endl;
    } else {
        std::cout << entry.str() << std::endl;
    }
}
