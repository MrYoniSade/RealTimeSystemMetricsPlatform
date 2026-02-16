#include "http_client.h"
#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <array>

/**
 * @brief Appends response payload bytes emitted by libcurl.
 *
 * libcurl calls this function one or more times as response chunks arrive.
 * The payload is appended to the std::string provided via CURLOPT_WRITEDATA.
 *
 * @param contents Pointer to the received bytes.
 * @param size Size of each element in bytes.
 * @param nmemb Number of elements.
 * @param userp Opaque user pointer expected to be std::string*.
 * @return Number of bytes consumed by this callback.
 */
static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

/**
 * @brief Creates an HTTP client bound to a backend base URL.
 *
 * Initializes global libcurl state for the current process. This class assumes
 * process-level startup/shutdown style usage consistent with the agent runtime.
 *
 * @param backend_url Base backend URL (for example: http://localhost:8000).
 */
HttpClient::HttpClient(const std::string& backend_url) 
    : backend_url(backend_url) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

/**
 * @brief Releases libcurl global resources.
 */
HttpClient::~HttpClient() {
    curl_global_cleanup();
}

/**
 * @brief Serializes metrics into the backend JSON schema.
 *
 * Numeric values are emitted with two decimal places for consistency and
 * stable downstream parsing.
 *
 * @param metrics Metrics snapshot to serialize.
 * @return JSON payload string for the ingest endpoint.
 */
std::string HttpClient::metrics_to_json(const SystemMetrics& metrics) {
    std::ostringstream json;
    
    json << "{";
    json << "\"timestamp\":" << metrics.timestamp << ",";
    json << "\"total_cpu_percent\":" << std::fixed << std::setprecision(2) << metrics.total_cpu_percent << ",";
    json << "\"top_processes\":[";
    
    for (size_t i = 0; i < metrics.top_processes.size(); ++i) {
        const auto& proc = metrics.top_processes[i];
        json << "{";
        json << "\"pid\":" << proc.pid << ",";
        json << "\"name\":\"" << proc.name << "\",";
        json << "\"cpu_percent\":" << std::fixed << std::setprecision(2) << proc.cpu_percent << ",";
        json << "\"memory_mb\":" << std::fixed << std::setprecision(2) << proc.memory_mb;
        json << "}";
        
        if (i < metrics.top_processes.size() - 1) {
            json << ",";
        }
    }
    
    json << "]";
    json << "}";
    
    return json.str();
}

/**
 * @brief Sends a metrics snapshot to the backend ingest endpoint.
 *
 * This method posts JSON to `${backend_url}/ingest/metrics` and captures
 * diagnostic state for callers:
 * - `last_error_message` is set on failure and cleared at the start.
 * - `last_status_code` stores the latest HTTP response code when available.
 *
 * @param metrics Metrics snapshot to send.
 * @return true on HTTP 2xx response; false on network, transport, or HTTP errors.
 */
bool HttpClient::send_metrics(const SystemMetrics& metrics) {
    last_error_message.clear();
    last_status_code = 0;

    CURL* curl = curl_easy_init();
    if (!curl) {
        last_error_message = "Failed to initialize CURL client";
        return false;
    }
    
    std::string json_data = metrics_to_json(metrics);
    std::string response;
    std::array<char, CURL_ERROR_SIZE> curl_error{};
    
    curl_easy_setopt(curl, CURLOPT_URL, (backend_url + "/ingest/metrics").c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(json_data.size()));
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_error.data());
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 3L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &last_status_code);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        last_error_message = "Network error while sending to " + backend_url + "/ingest/metrics: ";
        if (!curl_error[0]) {
            last_error_message += curl_easy_strerror(res);
        } else {
            last_error_message += curl_error.data();
        }
        return false;
    }

    if (last_status_code < 200 || last_status_code >= 300) {
        last_error_message = "Backend returned HTTP " + std::to_string(last_status_code);
        if (!response.empty()) {
            last_error_message += " with response: " + response;
        }
        return false;
    }
    
    return true;
}

/**
 * @brief Returns the most recent send error message.
 *
 * Empty string means the last send operation succeeded.
 */
const std::string& HttpClient::last_error() const {
    return last_error_message;
}

/**
 * @brief Returns the HTTP status from the most recent send attempt.
 *
 * Returns 0 when no status code was available (for example if request setup
 * or network transport failed before a response was received).
 */
long HttpClient::last_http_status() const {
    return last_status_code;
}
