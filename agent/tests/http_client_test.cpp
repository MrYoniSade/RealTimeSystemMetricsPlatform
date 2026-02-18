#include "http_client.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("HttpClient::metrics_to_json formats values with two decimals") {
    HttpClient client("http://localhost:1234");

    SystemMetrics metrics{};
    metrics.timestamp = 1700000000;
    metrics.total_cpu_percent = 12.345;
    metrics.per_core_cpu_percent = {10.0, 15.5};
    metrics.system_memory_total_mb = 16000.0;
    metrics.system_memory_used_mb = 8000.25;
    metrics.top_processes = {
        ProcessMetrics{123, "proc1", 1.2, 10.0, 6, 120.0, 80.0, 90},
        ProcessMetrics{456, "proc2", 98.765, 512.5, 12, 2048.5, 1024.25, 350}
    };

    const std::string expected =
        "{"
        "\"timestamp\":1700000000,"
        "\"total_cpu_percent\":12.35,"
        "\"per_core_cpu_percent\":[10.00,15.50],"
        "\"system_memory_total_mb\":16000.00,"
        "\"system_memory_used_mb\":8000.25,"
        "\"top_processes\":["
        "{\"pid\":123,\"name\":\"proc1\",\"cpu_percent\":1.20,\"memory_mb\":10.00,\"thread_count\":6,\"io_read_mb\":120.00,\"io_write_mb\":80.00,\"handle_count\":90},"
        "{\"pid\":456,\"name\":\"proc2\",\"cpu_percent\":98.77,\"memory_mb\":512.50,\"thread_count\":12,\"io_read_mb\":2048.50,\"io_write_mb\":1024.25,\"handle_count\":350}"
        "]"
        "}";

    CHECK(client.metrics_to_json(metrics) == expected);
}

TEST_CASE("HttpClient::metrics_to_json handles empty process list") {
    HttpClient client("http://localhost:1234");

    SystemMetrics metrics{};
    metrics.timestamp = 1700000001;
    metrics.total_cpu_percent = 0.0;
    metrics.per_core_cpu_percent = {};
    metrics.system_memory_total_mb = 0.0;
    metrics.system_memory_used_mb = 0.0;
    metrics.top_processes = {};

    const std::string expected =
        "{"
        "\"timestamp\":1700000001,"
        "\"total_cpu_percent\":0.00,"
        "\"per_core_cpu_percent\":[],"
        "\"system_memory_total_mb\":0.00,"
        "\"system_memory_used_mb\":0.00,"
        "\"top_processes\":[]"
        "}";

    CHECK(client.metrics_to_json(metrics) == expected);
}
