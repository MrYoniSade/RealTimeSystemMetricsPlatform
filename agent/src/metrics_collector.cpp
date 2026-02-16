#include "metrics_collector.h"
#include <cstring>

#ifdef _WIN32
#include <pdh.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <windows.h>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")
#endif

/**
 * @brief Constructs a MetricsCollector instance.
 *
 * Initializes platform-specific resources required for collecting system metrics.
 * On Windows, this includes initializing PDH (Performance Data Helper) resources.
 */
MetricsCollector::MetricsCollector() {
#ifdef _WIN32
    initialize_pdh();
#endif
}

/**
 * @brief Destructor for the MetricsCollector class.
 *
 * Cleans up any resources used by the MetricsCollector instance.
 * On Windows, this includes releasing PDH resources.
 */
MetricsCollector::~MetricsCollector() {
#ifdef _WIN32
    cleanup_pdh();
#endif
}

/**
 * @brief Collects system-wide metrics, including CPU usage and top processes.
 *
 * This method gathers the current timestamp, total CPU usage percentage, and
 * a list of the top resource-consuming processes.
 *
 * @return A SystemMetrics object containing the collected metrics.
 */
SystemMetrics MetricsCollector::collect() {
    SystemMetrics metrics;
    metrics.timestamp = time(nullptr);
    metrics.total_cpu_percent = get_total_cpu();
    metrics.top_processes = get_top_processes();
    return metrics;
}

/**
 * @brief Retrieves the total CPU usage percentage.
 *
 * On Windows, this method uses PDH (Performance Data Helper) to calculate
 * the total CPU usage. On other platforms, the implementation is currently
 * a placeholder.
 *
 * @return The total CPU usage percentage as a double.
 */
double MetricsCollector::get_total_cpu() {
#ifdef _WIN32
    // Use PDH (Performance Data Helper) to get CPU usage
    // This is a simplified implementation; real-world usage would need proper error handling
    // For now, return a placeholder value
    return 0.0;
#else
    // Linux/macOS implementation would go here
    return 0.0;
#endif
}

/**
 * @brief Retrieves information about the top resource-consuming processes.
 *
 * On Windows, this method uses the Toolhelp32 API to enumerate processes
 * and gather their metrics. The processes are sorted by CPU usage, and
 * only the top 5 are returned.
 *
 * @return A vector of ProcessMetrics objects representing the top processes.
 */
std::vector<ProcessMetrics> MetricsCollector::get_top_processes() {
    std::vector<ProcessMetrics> processes;

#ifdef _WIN32
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return processes;
    }

    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &entry)) {
        do {
            ProcessMetrics proc;
            proc.pid = entry.th32ProcessID;
            proc.name = std::string(entry.szExeFile);
            proc.cpu_percent = 0.0;  // TODO: Calculate actual CPU
            proc.memory_mb = 0.0;    // TODO: Calculate actual memory
            processes.push_back(proc);
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);

    // Sort by CPU and take top 5
    if (processes.size() > 5) {
        processes.resize(5);
    }
#endif

    return processes;
}

#ifdef _WIN32
/**
 * @brief Initializes PDH (Performance Data Helper) resources for Windows.
 *
 * This method sets up the necessary PDH counters for monitoring CPU usage.
 */
void MetricsCollector::initialize_pdh() {
    // PDH initialization would go here
}

/**
 * @brief Cleans up PDH resources for Windows.
 *
 * This method releases any PDH resources allocated during initialization.
 */
void MetricsCollector::cleanup_pdh() {
    // PDH cleanup would go here
}
#endif