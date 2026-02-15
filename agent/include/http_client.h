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

private:
    std::string backend_url; ///< The URL of the backend server.

    /**
     * @brief Converts system metrics into a JSON string.
     * @param metrics The system metrics to be converted.
     * @return A JSON string representation of the metrics.
     */
    std::string metrics_to_json(const SystemMetrics& metrics);
};