#include "http_client.h"
#include <curl/curl.h>
#include <sstream>
#include <iomanip>

// Callback for CURL to handle response
static size_t write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

HttpClient::HttpClient(const std::string& backend_url) 
    : backend_url(backend_url) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

HttpClient::~HttpClient() {
    curl_global_cleanup();
}

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

bool HttpClient::send_metrics(const SystemMetrics& metrics) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return false;
    }
    
    std::string json_data = metrics_to_json(metrics);
    std::string response;
    
    curl_easy_setopt(curl, CURLOPT_URL, (backend_url + "/ingest/metrics").c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_data.c_str());
    
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
    
    CURLcode res = curl_easy_perform(curl);
    
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    return res == CURLE_OK;
}
