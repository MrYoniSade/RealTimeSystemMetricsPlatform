#pragma once

#include <string>
#include <vector>
#include <ctime>

struct ProcessMetrics {
    int pid;
    std::string name;
    double cpu_percent;
    double memory_mb;
};

struct SystemMetrics {
    time_t timestamp;
    double total_cpu_percent;
    std::vector<ProcessMetrics> top_processes;
};

class MetricsCollector {
public:
    MetricsCollector();
    ~MetricsCollector();
    
    SystemMetrics collect();
    
private:
    double get_total_cpu();
    std::vector<ProcessMetrics> get_top_processes();
    
#ifdef _WIN32
    void initialize_pdh();
    void cleanup_pdh();
    void* cpu_counter = nullptr;
#endif
};
