#include "metrics_collector.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <thread>
#include <unordered_map>

namespace {
template <typename T>
constexpr T min_value(T left, T right) {
    return (left < right) ? left : right;
}

template <typename T>
constexpr T max_value(T left, T right) {
    return (left > right) ? left : right;
}

template <typename T>
constexpr T clamp_value(T value, T low, T high) {
    return (value < low) ? low : ((value > high) ? high : value);
}
}  // namespace

#if defined(__linux__)
#include <unistd.h>
#include <sys/sysinfo.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
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

bool get_system_times(uint64_t& idle_time, uint64_t& total_time) {
    FILETIME idle_file_time;
    FILETIME kernel_file_time;
    FILETIME user_file_time;
    if (!GetSystemTimes(&idle_file_time, &kernel_file_time, &user_file_time)) {
        idle_time = 0;
        total_time = 0;
        return false;
    }

    idle_time = filetime_to_uint64(idle_file_time);
    total_time = filetime_to_uint64(kernel_file_time) + filetime_to_uint64(user_file_time);
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

std::string process_name_to_utf8(const TCHAR* process_name) {
#ifdef UNICODE
    if (process_name == nullptr || *process_name == L'\0') {
        return std::string();
    }

    const int required = WideCharToMultiByte(CP_UTF8, 0, process_name, -1, nullptr, 0, nullptr, nullptr);
    if (required <= 1) {
        return std::string();
    }

    std::string utf8_name(static_cast<size_t>(required), '\0');
    const int written =
        WideCharToMultiByte(CP_UTF8, 0, process_name, -1, utf8_name.data(), required, nullptr, nullptr);
    if (written <= 1) {
        return std::string();
    }
    utf8_name.resize(static_cast<size_t>(written - 1));
    return utf8_name;
#else
    return (process_name != nullptr) ? std::string(process_name) : std::string();
#endif
}
}  // namespace
#endif

#if defined(__linux__)
namespace {
bool read_linux_cpu_times(uint64_t& idle_time, uint64_t& total_time) {
    std::ifstream stat_file("/proc/stat");
    if (!stat_file.is_open()) {
        idle_time = 0;
        total_time = 0;
        return false;
    }

    std::string cpu_label;
    uint64_t user = 0;
    uint64_t nice = 0;
    uint64_t system = 0;
    uint64_t idle = 0;
    uint64_t iowait = 0;
    uint64_t irq = 0;
    uint64_t softirq = 0;
    uint64_t steal = 0;

    stat_file >> cpu_label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;

    if (!stat_file.good() || cpu_label != "cpu") {
        idle_time = 0;
        total_time = 0;
        return false;
    }

    idle_time = idle + iowait;
    total_time = user + nice + system + idle + iowait + irq + softirq + steal;
    return true;
}

struct LinuxProcessSnapshot {
    int pid;
    std::string name;
    uint64_t cpu_time;
    double memory_mb;
    int thread_count;
    double io_read_mb;
    double io_write_mb;
    int handle_count;
};

struct LinuxCpuTimes {
    uint64_t idle_time;
    uint64_t total_time;
};

bool is_numeric_text(const char* text) {
    if (text == nullptr || *text == '\0') {
        return false;
    }

    for (const char* current = text; *current != '\0'; ++current) {
        if (!std::isdigit(static_cast<unsigned char>(*current))) {
            return false;
        }
    }

    return true;
}

bool read_linux_process_stat(int pid, std::string& process_name, uint64_t& cpu_time, uint64_t& rss_pages) {
    const std::string stat_path = std::string("/proc/") + std::to_string(pid) + "/stat";
    std::ifstream stat_file(stat_path);
    if (!stat_file.is_open()) {
        return false;
    }

    std::string line;
    std::getline(stat_file, line);
    if (line.empty()) {
        return false;
    }

    const std::size_t open_paren = line.find('(');
    const std::size_t close_paren = line.rfind(')');
    if (open_paren == std::string::npos || close_paren == std::string::npos || close_paren <= open_paren) {
        return false;
    }

    process_name = line.substr(open_paren + 1, close_paren - open_paren - 1);

    std::string tail = line.substr(close_paren + 2);
    std::istringstream stream(tail);
    std::vector<std::string> fields;
    std::string token;
    while (stream >> token) {
        fields.push_back(token);
    }

    if (fields.size() < 22) {
        return false;
    }

    try {
        const uint64_t user_ticks = std::stoull(fields[11]);
        const uint64_t system_ticks = std::stoull(fields[12]);
        cpu_time = user_ticks + system_ticks;
        rss_pages = std::stoull(fields[21]);
    } catch (...) {
        return false;
    }

    return true;
}

bool read_linux_process_status_fields(int pid, int& thread_count, int& handle_count) {
    thread_count = 0;
    handle_count = 0;

    const std::string status_path = std::string("/proc/") + std::to_string(pid) + "/status";
    std::ifstream status_file(status_path);
    if (!status_file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(status_file, line)) {
        if (line.rfind("Threads:", 0) == 0) {
            std::istringstream parser(line.substr(8));
            parser >> thread_count;
            break;
        }
    }

    std::error_code fd_error;
    const auto fd_dir = std::filesystem::path("/proc") / std::to_string(pid) / "fd";
    int fd_count = 0;
    for (const auto& _ : std::filesystem::directory_iterator(fd_dir, fd_error)) {
        (void)_;
        if (fd_error) {
            break;
        }
        ++fd_count;
    }
    if (!fd_error) {
        handle_count = fd_count;
    }

    return true;
}

bool read_linux_process_io(int pid, double& io_read_mb, double& io_write_mb) {
    io_read_mb = 0.0;
    io_write_mb = 0.0;

    const std::string io_path = std::string("/proc/") + std::to_string(pid) + "/io";
    std::ifstream io_file(io_path);
    if (!io_file.is_open()) {
        return false;
    }

    uint64_t read_bytes = 0;
    uint64_t write_bytes = 0;
    std::string key;
    uint64_t value = 0;

    while (io_file >> key >> value) {
        if (key == "read_bytes:") {
            read_bytes = value;
        } else if (key == "write_bytes:") {
            write_bytes = value;
        }
    }

    io_read_mb = static_cast<double>(read_bytes) / (1024.0 * 1024.0);
    io_write_mb = static_cast<double>(write_bytes) / (1024.0 * 1024.0);
    return true;
}

bool read_linux_per_core_cpu_times(std::vector<LinuxCpuTimes>& core_times) {
    core_times.clear();

    std::ifstream stat_file("/proc/stat");
    if (!stat_file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(stat_file, line)) {
        if (line.rfind("cpu", 0) != 0 || (line.size() > 3 && !std::isdigit(static_cast<unsigned char>(line[3])))) {
            continue;
        }

        std::istringstream parser(line);
        std::string label;
        uint64_t user = 0;
        uint64_t nice = 0;
        uint64_t system = 0;
        uint64_t idle = 0;
        uint64_t iowait = 0;
        uint64_t irq = 0;
        uint64_t softirq = 0;
        uint64_t steal = 0;

        parser >> label >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        if (!parser.good() && !parser.eof()) {
            continue;
        }

        LinuxCpuTimes times;
        times.idle_time = idle + iowait;
        times.total_time = user + nice + system + idle + iowait + irq + softirq + steal;
        core_times.push_back(times);
    }

    return !core_times.empty();
}

std::unordered_map<int, LinuxProcessSnapshot> collect_linux_process_snapshot() {
    std::unordered_map<int, LinuxProcessSnapshot> snapshots;
    snapshots.reserve(512);

    const long page_size = sysconf(_SC_PAGESIZE);
    std::error_code iter_error;
    for (const auto& entry : std::filesystem::directory_iterator("/proc", iter_error)) {
        if (iter_error) {
            continue;
        }

        const std::string pid_text = entry.path().filename().string();
        if (!is_numeric_text(pid_text.c_str())) {
            continue;
        }

        const int pid = std::atoi(pid_text.c_str());
        if (pid <= 0) {
            continue;
        }

        LinuxProcessSnapshot snapshot;
        snapshot.pid = pid;
        snapshot.cpu_time = 0;
        snapshot.memory_mb = 0.0;
        snapshot.thread_count = 0;
        snapshot.io_read_mb = 0.0;
        snapshot.io_write_mb = 0.0;
        snapshot.handle_count = 0;

        uint64_t rss_pages = 0;
        if (!read_linux_process_stat(pid, snapshot.name, snapshot.cpu_time, rss_pages)) {
            continue;
        }

        if (page_size > 0) {
            snapshot.memory_mb =
                (static_cast<double>(rss_pages) * static_cast<double>(page_size)) / (1024.0 * 1024.0);
        }

        read_linux_process_status_fields(pid, snapshot.thread_count, snapshot.handle_count);
        read_linux_process_io(pid, snapshot.io_read_mb, snapshot.io_write_mb);

        snapshots[pid] = std::move(snapshot);
    }
    return snapshots;
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
    metrics.per_core_cpu_percent = get_per_core_cpu();
    const auto [memory_total_mb, memory_used_mb] = get_system_memory();
    metrics.system_memory_total_mb = memory_total_mb;
    metrics.system_memory_used_mb = memory_used_mb;
    metrics.top_processes = get_top_processes();
    return metrics;
}

std::vector<double> MetricsCollector::get_per_core_cpu() {
    std::vector<double> per_core_cpu;

#ifdef _WIN32
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
    if (system_info.dwNumberOfProcessors > 0) {
        per_core_cpu.assign(static_cast<size_t>(system_info.dwNumberOfProcessors), 0.0);
    }
#elif defined(__linux__)
    static bool has_previous = false;
    static std::vector<LinuxCpuTimes> previous_core_times;

    std::vector<LinuxCpuTimes> current_core_times;
    if (!read_linux_per_core_cpu_times(current_core_times)) {
        return per_core_cpu;
    }

    if (!has_previous) {
        has_previous = true;
        previous_core_times = current_core_times;
        per_core_cpu.assign(current_core_times.size(), 0.0);
        return per_core_cpu;
    }

    const size_t core_count = min_value(previous_core_times.size(), current_core_times.size());
    per_core_cpu.reserve(core_count);
    for (size_t index = 0; index < core_count; ++index) {
        const auto& previous = previous_core_times[index];
        const auto& current = current_core_times[index];

        const uint64_t total_delta =
            (current.total_time > previous.total_time) ? (current.total_time - previous.total_time) : 0;
        const uint64_t idle_delta =
            (current.idle_time > previous.idle_time) ? (current.idle_time - previous.idle_time) : 0;

        double usage = 0.0;
        if (total_delta > 0) {
            usage =
                (static_cast<double>(total_delta - min_value(total_delta, idle_delta)) /
                 static_cast<double>(total_delta)) *
                100.0;
        }

            per_core_cpu.push_back(clamp_value(usage, 0.0, 100.0));
    }

    previous_core_times = std::move(current_core_times);
#endif

    return per_core_cpu;
}

std::pair<double, double> MetricsCollector::get_system_memory() {
#ifdef _WIN32
    MEMORYSTATUSEX memory_status;
    std::memset(&memory_status, 0, sizeof(memory_status));
    memory_status.dwLength = sizeof(memory_status);

    if (!GlobalMemoryStatusEx(&memory_status)) {
        return {0.0, 0.0};
    }

    const double total_mb = static_cast<double>(memory_status.ullTotalPhys) / (1024.0 * 1024.0);
    const double available_mb = static_cast<double>(memory_status.ullAvailPhys) / (1024.0 * 1024.0);
    const double used_mb = max_value(0.0, total_mb - available_mb);
    return {total_mb, used_mb};
#elif defined(__linux__)
    std::ifstream meminfo_file("/proc/meminfo");
    if (!meminfo_file.is_open()) {
        return {0.0, 0.0};
    }

    uint64_t mem_total_kb = 0;
    uint64_t mem_available_kb = 0;
    std::string key;
    uint64_t value = 0;
    std::string unit;

    while (meminfo_file >> key >> value >> unit) {
        if (key == "MemTotal:") {
            mem_total_kb = value;
        } else if (key == "MemAvailable:") {
            mem_available_kb = value;
        }
    }

    const double total_mb = static_cast<double>(mem_total_kb) / 1024.0;
    const double available_mb = static_cast<double>(mem_available_kb) / 1024.0;
    const double used_mb = max_value(0.0, total_mb - available_mb);
    return {total_mb, used_mb};
#else
    return {0.0, 0.0};
#endif
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
    static bool has_previous = false;
    static uint64_t previous_idle = 0;
    static uint64_t previous_total = 0;

    uint64_t current_idle = 0;
    uint64_t current_total = 0;
    if (!get_system_times(current_idle, current_total)) {
        return 0.0;
    }

    if (!has_previous) {
        has_previous = true;
        previous_idle = current_idle;
        previous_total = current_total;
        return 0.0;
    }

    const uint64_t total_delta =
        (current_total > previous_total) ? (current_total - previous_total) : 0;
    const uint64_t idle_delta =
        (current_idle > previous_idle) ? (current_idle - previous_idle) : 0;

    previous_idle = current_idle;
    previous_total = current_total;

    if (total_delta == 0) {
        return 0.0;
    }

    const double usage =
        (static_cast<double>(total_delta - min_value(total_delta, idle_delta)) /
         static_cast<double>(total_delta)) *
        100.0;
    return clamp_value(usage, 0.0, 100.0);
#elif defined(__linux__)
    static bool has_previous = false;
    static uint64_t previous_idle = 0;
    static uint64_t previous_total = 0;

    uint64_t current_idle = 0;
    uint64_t current_total = 0;
    if (!read_linux_cpu_times(current_idle, current_total)) {
        return 0.0;
    }

    if (!has_previous) {
        has_previous = true;
        previous_idle = current_idle;
        previous_total = current_total;
        return 0.0;
    }

    const uint64_t total_delta =
        (current_total > previous_total) ? (current_total - previous_total) : 0;
    const uint64_t idle_delta =
        (current_idle > previous_idle) ? (current_idle - previous_idle) : 0;

    previous_idle = current_idle;
    previous_total = current_total;

    if (total_delta == 0) {
        return 0.0;
    }

    const double usage =
        (static_cast<double>(total_delta - min_value(total_delta, idle_delta)) /
         static_cast<double>(total_delta)) *
        100.0;
    return clamp_value(usage, 0.0, 100.0);
#else
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
            proc.name = process_name_to_utf8(entry.szExeFile);
            proc.cpu_percent = 0.0;
            proc.memory_mb = 0.0;
            proc.thread_count = static_cast<int>(entry.cntThreads);
            proc.io_read_mb = 0.0;
            proc.io_write_mb = 0.0;
            proc.handle_count = 0;

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

        IO_COUNTERS io_counters;
        std::memset(&io_counters, 0, sizeof(io_counters));
        if (GetProcessIoCounters(process_handle, &io_counters)) {
            proc.io_read_mb = static_cast<double>(io_counters.ReadTransferCount) / (1024.0 * 1024.0);
            proc.io_write_mb = static_cast<double>(io_counters.WriteTransferCount) / (1024.0 * 1024.0);
        }

        DWORD handle_count = 0;
        if (GetProcessHandleCount(process_handle, &handle_count)) {
            proc.handle_count = static_cast<int>(handle_count);
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
#elif defined(__linux__)
    uint64_t system_idle_start = 0;
    uint64_t system_total_start = 0;
    if (!read_linux_cpu_times(system_idle_start, system_total_start)) {
        return processes;
    }

    const auto start_snapshot = collect_linux_process_snapshot();

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    uint64_t system_idle_end = 0;
    uint64_t system_total_end = 0;
    if (!read_linux_cpu_times(system_idle_end, system_total_end)) {
        return processes;
    }

    const uint64_t system_total_delta =
        (system_total_end > system_total_start) ? (system_total_end - system_total_start) : 0;
    if (system_total_delta == 0) {
        return processes;
    }

    const auto end_snapshot = collect_linux_process_snapshot();

    processes.reserve(end_snapshot.size());
    for (const auto& [pid, end_process] : end_snapshot) {
        const auto start_it = start_snapshot.find(pid);
        if (start_it == start_snapshot.end()) {
            continue;
        }

        const uint64_t start_cpu = start_it->second.cpu_time;
        if (end_process.cpu_time < start_cpu) {
            continue;
        }

        const uint64_t process_delta = end_process.cpu_time - start_cpu;

        ProcessMetrics proc;
        proc.pid = pid;
        proc.name = end_process.name;
        proc.cpu_percent =
            (static_cast<double>(process_delta) / static_cast<double>(system_total_delta)) * 100.0;
        proc.memory_mb = end_process.memory_mb;
        proc.thread_count = end_process.thread_count;
        proc.io_read_mb = end_process.io_read_mb;
        proc.io_write_mb = end_process.io_write_mb;
        proc.handle_count = end_process.handle_count;
        processes.push_back(proc);
    }

    std::sort(processes.begin(), processes.end(), [](const ProcessMetrics& left, const ProcessMetrics& right) {
        if (left.cpu_percent != right.cpu_percent) {
            return left.cpu_percent > right.cpu_percent;
        }
        return left.memory_mb > right.memory_mb;
    });

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