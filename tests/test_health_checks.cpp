#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <thread>

#include "sentinel/health/health_checks.hpp"

using namespace sentinel::health;

static uint64_t unix_now_s() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

static HealthCheckInputs make_inputs(
    Heartbeat* es,
    Heartbeat* re,
    Heartbeat* disp,
    uint64_t rpc_ts = 0,
    bool db_ok = true)
{
    return HealthCheckInputs{
        .event_source            = es,
        .risk_engine             = re,
        .dispatcher              = disp,
        .rpc_last_success_unix_s = [rpc_ts]() { return rpc_ts; },
        .db_probe                = [db_ok]() { return db_ok; },
    };
}

TEST_CASE("evaluate_liveness always returns ok", "[health]") {
    CheckResult r = evaluate_liveness();
    REQUIRE(r.ok);
    REQUIRE(r.detail.empty());
}

TEST_CASE("evaluate_readiness: all checks pass", "[health]") {
    Heartbeat es, re, disp;
    es.record(); re.record(); disp.record();

    uint64_t recent_ts = unix_now_s() - 10; // 10 seconds ago

    auto inputs = make_inputs(&es, &re, &disp, recent_ts, true);
    HealthChecksConfig cfg;
    auto report = evaluate_readiness(inputs, cfg);

    REQUIRE(report.ready);
    REQUIRE(report.checks.at("threads_alive").ok);
    REQUIRE(report.checks.at("db_connected").ok);
    REQUIRE(report.checks.at("rpc_recent").ok);
    REQUIRE(report.checks.at("heartbeats_recorded").ok);
}

TEST_CASE("evaluate_readiness: stale heartbeat -> threads_alive fails", "[health]") {
    Heartbeat es, re, disp;
    es.record(); re.record(); disp.record();

    // Sleep past the (very short) max_age threshold
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto inputs = make_inputs(&es, &re, &disp, 0, true);
    HealthChecksConfig cfg;
    cfg.heartbeat_max_age_ms = 50; // stale after 50ms

    auto report = evaluate_readiness(inputs, cfg);

    REQUIRE_FALSE(report.ready);
    REQUIRE_FALSE(report.checks.at("threads_alive").ok);
    REQUIRE_FALSE(report.checks.at("threads_alive").detail.empty());

    const auto& detail = report.checks.at("threads_alive").detail;
    REQUIRE(detail.find("event_source") != std::string::npos);
    REQUIRE(detail.find("risk_engine")  != std::string::npos);
    REQUIRE(detail.find("dispatcher")   != std::string::npos);
    REQUIRE(detail.find("stale")        != std::string::npos);
}

TEST_CASE("evaluate_readiness: DB probe fails -> not ready", "[health]") {
    Heartbeat es, re, disp;
    es.record(); re.record(); disp.record();

    auto inputs = make_inputs(&es, &re, &disp, 0, false);
    HealthChecksConfig cfg;
    auto report = evaluate_readiness(inputs, cfg);

    REQUIRE_FALSE(report.ready);
    REQUIRE_FALSE(report.checks.at("db_connected").ok);
    REQUIRE_FALSE(report.checks.at("db_connected").detail.empty());
}

TEST_CASE("evaluate_readiness: rpc_last_success == 0 -> startup grace, rpc_recent ok", "[health]") {
    Heartbeat es, re, disp;
    es.record(); re.record(); disp.record();

    auto inputs = make_inputs(&es, &re, &disp, 0, true);
    HealthChecksConfig cfg;
    auto report = evaluate_readiness(inputs, cfg);

    REQUIRE(report.checks.at("rpc_recent").ok);
    REQUIRE(report.checks.at("rpc_recent").detail == "no RPC calls yet");
}

TEST_CASE("evaluate_readiness: RPC last_success older than rpc_max_silence_ms -> not ready", "[health]") {
    Heartbeat es, re, disp;
    es.record(); re.record(); disp.record();

    // 5 minutes ago; max silence is 2 minutes
    uint64_t old_ts = unix_now_s() - 300;

    auto inputs = make_inputs(&es, &re, &disp, old_ts, true);
    HealthChecksConfig cfg;
    cfg.rpc_max_silence_ms = 120'000;

    auto report = evaluate_readiness(inputs, cfg);

    REQUIRE_FALSE(report.ready);
    REQUIRE_FALSE(report.checks.at("rpc_recent").ok);
    REQUIRE_FALSE(report.checks.at("rpc_recent").detail.empty());
}

TEST_CASE("evaluate_readiness: heartbeat never recorded -> heartbeats_recorded fails", "[health]") {
    Heartbeat es, re, disp;
    // Intentionally do not call record() on any heartbeat

    auto inputs = make_inputs(&es, &re, &disp, 0, true);
    HealthChecksConfig cfg;
    auto report = evaluate_readiness(inputs, cfg);

    REQUIRE_FALSE(report.ready);
    REQUIRE_FALSE(report.checks.at("heartbeats_recorded").ok);
    REQUIRE_FALSE(report.checks.at("heartbeats_recorded").detail.empty());

    const auto& detail = report.checks.at("heartbeats_recorded").detail;
    REQUIRE(detail.find("event_source") != std::string::npos);
    REQUIRE(detail.find("risk_engine")  != std::string::npos);
    REQUIRE(detail.find("dispatcher")   != std::string::npos);
}

TEST_CASE("evaluate_readiness: only stale heartbeats appear in detail", "[health]") {
    Heartbeat fresh_es, stale_re, fresh_disp;
    fresh_es.record();
    stale_re.record();
    fresh_disp.record();

    // Age out only risk_engine
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    fresh_es.record();
    fresh_disp.record();
    // stale_re intentionally not re-recorded

    auto inputs = make_inputs(&fresh_es, &stale_re, &fresh_disp, 0, true);
    HealthChecksConfig cfg;
    cfg.heartbeat_max_age_ms = 50;
    auto report = evaluate_readiness(inputs, cfg);

    REQUIRE_FALSE(report.ready);
    const auto& detail = report.checks.at("threads_alive").detail;
    REQUIRE(detail.find("risk_engine")  != std::string::npos);
    REQUIRE(detail.find("event_source") == std::string::npos);
    REQUIRE(detail.find("dispatcher")   == std::string::npos);
}
