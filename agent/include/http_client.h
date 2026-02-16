#pragma once

#include <string>
#include "metrics_collector.h"

/**
 * @class HttpClient
 * @brief A class responsible for sending system metrics to a backend server via HTTP.
 *
 * This class provides functionality to serialize system metrics into JSON format
 * and send them to a specified backend URL using HTTP POST requests.
 */
class HttpClient {
public:
    /**
     * @brief Constructs an HttpClient with the specified backend URL.
     * @param backend_url The URL of the backend server to which metrics will be sent.
     */
    HttpClient(const std::string& backend_url);

    /**
     * @brief Destructor for the HttpClient class.
     */
    ~HttpClient();

    /**
     * @brief Sends system metrics to the backend server.
     * @param metrics The system metrics to be sent.
     * @return True if the metrics were successfully sent, false otherwise.
     */
    bool send_metrics(const SystemMetrics& metrics);

    /**
     * @brief Gets the last error message from send_metrics.
     * @return A human-readable error message. Empty if the last send succeeded.
     */
    const std::string& last_error() const;

    /**
     * @brief Gets the last HTTP status code returned by the backend.
     * @return HTTP status code, or 0 if unavailable.
     */
    long last_http_status() const;

    /**
     * @brief Converts system metrics into a JSON string.
     * @param metrics The system metrics to be converted.
     * @return A JSON string representation of the metrics.
     */
    std::string metrics_to_json(const SystemMetrics& metrics);

private:
    std::string backend_url; ///< The URL of the backend server.
    std::string last_error_message;
    long last_status_code = 0;
};