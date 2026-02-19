#include "metrics_collector.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <ctime>

TEST_CASE("MetricsCollector::collect returns valid timestamp and bounded process list") {
    MetricsCollector collector;

    const std::time_t before = std::time(nullptr);
    const SystemMetrics metrics = collector.collect();
    const std::time_t after = std::time(nullptr);

    CHECK(metrics.timestamp >= before);
    CHECK(metrics.timestamp <= after);
    CHECK(metrics.top_processes.size() <= 12);
    CHECK(std::isfinite(metrics.total_cpu_percent));
    CHECK(metrics.total_cpu_percent >= 0.0);
    CHECK(std::isfinite(metrics.system_memory_total_mb));
    CHECK(std::isfinite(metrics.system_memory_used_mb));
    CHECK(metrics.system_memory_total_mb >= 0.0);
    CHECK(metrics.system_memory_used_mb >= 0.0);
    CHECK(metrics.system_memory_used_mb <= metrics.system_memory_total_mb + 1.0);

    for (const auto cpu_value : metrics.per_core_cpu_percent) {
        CHECK(std::isfinite(cpu_value));
        CHECK(cpu_value >= 0.0);
        CHECK(cpu_value <= 100.0);
    }
}

TEST_CASE("MetricsCollector::collect returns non-negative per-process metrics") {
    MetricsCollector collector;
    const SystemMetrics metrics = collector.collect();

    for (const auto& process : metrics.top_processes) {
        CHECK(process.pid >= 0);
        CHECK_FALSE(process.name.empty());
        CHECK(std::isfinite(process.cpu_percent));
        CHECK(std::isfinite(process.memory_mb));
        CHECK(process.cpu_percent >= 0.0);
        CHECK(process.memory_mb >= 0.0);
        CHECK(process.thread_count >= 0);
        CHECK(std::isfinite(process.io_read_mb));
        CHECK(std::isfinite(process.io_write_mb));
        CHECK(process.io_read_mb >= 0.0);
        CHECK(process.io_write_mb >= 0.0);
        CHECK(process.handle_count >= 0);
    }
}
