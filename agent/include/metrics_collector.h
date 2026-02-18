#pragma once

#include <string>
#include <vector>
#include <ctime>
#include <utility>

/**
 * @struct ProcessMetrics
 * @brief Represents metrics for an individual process.
 *
 * This structure contains information about a process, including its
 * PID, name, CPU usage percentage, and memory usage in MB.
 */
struct ProcessMetrics {
    int pid; ///< Process ID.
    std::string name; ///< Name of the process.
    double cpu_percent; ///< CPU usage percentage of the process.
    double memory_mb; ///< Memory usage of the process in MB.
    int thread_count; ///< Number of threads in the process.
    double io_read_mb; ///< Process read I/O volume in MB.
    double io_write_mb; ///< Process write I/O volume in MB.
    int handle_count; ///< Number of process handles (or file descriptors on Linux).
};

/**
 * @struct SystemMetrics
 * @brief Represents system-wide metrics.
 *
 * This structure contains a timestamp, total CPU usage percentage,
 * and a list of the top processes by resource usage.
 */
struct SystemMetrics {
    time_t timestamp; ///< Timestamp of when the metrics were collected.
    double total_cpu_percent; ///< Total CPU usage percentage.
    std::vector<double> per_core_cpu_percent; ///< CPU usage percentage per core.
    double system_memory_total_mb; ///< Total system memory in MB.
    double system_memory_used_mb; ///< Used system memory in MB.
    std::vector<ProcessMetrics> top_processes; ///< List of top processes by resource usage.
};

/**
 * @class MetricsCollector
 * @brief Collects system metrics, including CPU usage and process information.
 *
 * This class provides methods to collect system-wide metrics and
 * information about the top resource-consuming processes. It is
 * designed to work on both Windows and non-Windows platforms.
 */
class MetricsCollector {
public:
    /**
     * @brief Constructs a MetricsCollector instance.
     *
     * Initializes any platform-specific resources required for
     * collecting metrics.
     */
    MetricsCollector();

    /**
     * @brief Destructor for the MetricsCollector class.
     *
     * Cleans up any resources used by the MetricsCollector instance.
     */
    ~MetricsCollector();

    /**
     * @brief Collects system metrics.
     * @return A SystemMetrics object containing the collected metrics.
     *
     * This method gathers system-wide metrics, including total CPU usage
     * and information about the top processes.
     */
    SystemMetrics collect();

private:
    /**
     * @brief Retrieves the total CPU usage percentage.
     * @return The total CPU usage percentage.
     */
    double get_total_cpu();

    /**
     * @brief Retrieves per-core CPU usage percentages.
     * @return A vector containing CPU usage percentage for each core.
     */
    std::vector<double> get_per_core_cpu();

    /**
     * @brief Retrieves total and used system memory in MB.
     * @return A pair of (total_mb, used_mb).
     */
    std::pair<double, double> get_system_memory();

    /**
     * @brief Retrieves information about the top resource-consuming processes.
     * @return A vector of ProcessMetrics objects representing the top processes.
     */
    std::vector<ProcessMetrics> get_top_processes();

#ifdef _WIN32
    /**
     * @brief Initializes PDH (Performance Data Helper) resources for Windows.
     */
    void initialize_pdh();

    /**
     * @brief Cleans up PDH resources for Windows.
     */
    void cleanup_pdh();

    void* cpu_counter = nullptr; ///< Handle to the PDH CPU counter.
#endif
};