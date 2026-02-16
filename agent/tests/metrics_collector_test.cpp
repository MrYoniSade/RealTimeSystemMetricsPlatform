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
    CHECK(metrics.top_processes.size() <= 5);
    CHECK(std::isfinite(metrics.total_cpu_percent));
    CHECK(metrics.total_cpu_percent >= 0.0);
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
    }
}
