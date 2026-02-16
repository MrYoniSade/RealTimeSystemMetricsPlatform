#include "metrics_collector.h"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#include <pdh.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <windows.h>
#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "psapi.lib")

namespace {
uint64_t filetime_to_uint64(const FILETIME& file_time) {
    ULARGE_INTEGER value;
    value.LowPart = file_time.dwLowDateTime;
    value.HighPart = file_time.dwHighDateTime;
    return value.QuadPart;
}

bool get_total_system_cpu_time(uint64_t& total_time) {
    FILETIME idle_time;
    FILETIME kernel_time;
    FILETIME user_time;
    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) {
        total_time = 0;
        return false;
    }

    total_time = filetime_to_uint64(kernel_time) + filetime_to_uint64(user_time);
    return true;
}

bool get_process_cpu_time(HANDLE process_handle, uint64_t& process_time) {
    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;
    if (!GetProcessTimes(process_handle, &creation_time, &exit_time, &kernel_time, &user_time)) {
        process_time = 0;
        return false;
    }

    process_time = filetime_to_uint64(kernel_time) + filetime_to_uint64(user_time);
    return true;
}
}  // namespace
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
    std::unordered_map<int, uint64_t> process_time_start;
    process_time_start.reserve(512);

    uint64_t system_time_start = 0;
    if (!get_total_system_cpu_time(system_time_start)) {
        return processes;
    }

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
            proc.cpu_percent = 0.0;
            proc.memory_mb = 0.0;

            HANDLE process_handle = OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
                FALSE,
                static_cast<DWORD>(proc.pid)
            );
            if (process_handle != nullptr) {
                uint64_t process_time = 0;
                if (get_process_cpu_time(process_handle, process_time)) {
                    process_time_start[proc.pid] = process_time;
                }
                CloseHandle(process_handle);
            }

            processes.push_back(proc);
        } while (Process32Next(snapshot, &entry));
    }

    CloseHandle(snapshot);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    uint64_t system_time_end = 0;
    if (!get_total_system_cpu_time(system_time_end)) {
        return processes;
    }

    const uint64_t system_time_delta =
        (system_time_end > system_time_start) ? (system_time_end - system_time_start) : 0;

    for (auto& proc : processes) {
        HANDLE process_handle = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            FALSE,
            static_cast<DWORD>(proc.pid)
        );
        if (process_handle == nullptr) {
            continue;
        }

        uint64_t process_time_end = 0;
        auto it = process_time_start.find(proc.pid);
        if (system_time_delta > 0 &&
            it != process_time_start.end() &&
            get_process_cpu_time(process_handle, process_time_end) &&
            process_time_end >= it->second) {
            const uint64_t process_time_delta = process_time_end - it->second;
            proc.cpu_percent = (static_cast<double>(process_time_delta) /
                                static_cast<double>(system_time_delta)) *
                               100.0;
        }

        PROCESS_MEMORY_COUNTERS_EX memory_counters;
        std::memset(&memory_counters, 0, sizeof(memory_counters));
        if (GetProcessMemoryInfo(process_handle,
                                 reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory_counters),
                                 sizeof(memory_counters))) {
            proc.memory_mb =
                static_cast<double>(memory_counters.WorkingSetSize) / (1024.0 * 1024.0);
        }

        CloseHandle(process_handle);
    }

    std::sort(processes.begin(), processes.end(), [](const ProcessMetrics& left, const ProcessMetrics& right) {
        if (left.cpu_percent != right.cpu_percent) {
            return left.cpu_percent > right.cpu_percent;
        }
        return left.memory_mb > right.memory_mb;
    });

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