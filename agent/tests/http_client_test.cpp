#include "http_client.h"
#include <catch2/catch_test_macros.hpp>

TEST_CASE("HttpClient::metrics_to_json formats values with two decimals") {
    HttpClient client("http://localhost:1234");

    SystemMetrics metrics{};
    metrics.timestamp = 1700000000;
    metrics.total_cpu_percent = 12.345;
    metrics.top_processes = {
        ProcessMetrics{123, "proc1", 1.2, 10.0},
        ProcessMetrics{456, "proc2", 98.765, 512.5}
    };

    const std::string expected =
        "{"
        "\"timestamp\":1700000000,"
        "\"total_cpu_percent\":12.35,"
        "\"top_processes\":["
        "{\"pid\":123,\"name\":\"proc1\",\"cpu_percent\":1.20,\"memory_mb\":10.00},"
        "{\"pid\":456,\"name\":\"proc2\",\"cpu_percent\":98.77,\"memory_mb\":512.50}"
        "]"
        "}";

    CHECK(client.metrics_to_json(metrics) == expected);
}

TEST_CASE("HttpClient::metrics_to_json handles empty process list") {
    HttpClient client("http://localhost:1234");

    SystemMetrics metrics{};
    metrics.timestamp = 1700000001;
    metrics.total_cpu_percent = 0.0;
    metrics.top_processes = {};

    const std::string expected =
        "{"
        "\"timestamp\":1700000001,"
        "\"total_cpu_percent\":0.00,"
        "\"top_processes\":[]"
        "}";

    CHECK(client.metrics_to_json(metrics) == expected);
}
