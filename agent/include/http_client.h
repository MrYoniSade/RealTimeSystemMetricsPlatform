#pragma once

#include <string>
#include "metrics_collector.h"

class HttpClient {
public:
    HttpClient(const std::string& backend_url);
    ~HttpClient();
    
    bool send_metrics(const SystemMetrics& metrics);
    
private:
    std::string backend_url;
    std::string metrics_to_json(const SystemMetrics& metrics);
};
