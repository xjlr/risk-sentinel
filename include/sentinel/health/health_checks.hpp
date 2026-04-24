#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "sentinel/health/heartbeat.hpp"

namespace sentinel::health {

struct CheckResult {
    bool ok;
    std::string detail;  // short reason if !ok, empty otherwise
};

struct HealthChecksConfig {
    uint64_t heartbeat_max_age_ms = 5'000;   // thread stuck if older
    uint64_t rpc_max_silence_ms   = 120'000; // 2 minutes
};

struct HealthCheckInputs {
    // Monotonic heartbeats (steady_clock-based).
    const Heartbeat* event_source;
    const Heartbeat* risk_engine;
    const Heartbeat* dispatcher;

    // Wall-clock-based last-RPC-success (from Metrics), in Unix seconds.
    // 0 = never seen a success yet.
    std::function<uint64_t()> rpc_last_success_unix_s;

    // DB probe: returns true on success of "SELECT 1".
    std::function<bool()> db_probe;
};

struct ReadinessReport {
    bool ready;
    std::unordered_map<std::string, CheckResult> checks;
};

// Readiness: runs all checks, returns the full per-check report.
ReadinessReport evaluate_readiness(const HealthCheckInputs& inputs,
                                   const HealthChecksConfig& cfg);

// Liveness: minimal — only that this process is responding.
// Returns ok unconditionally; future expansions may add lenient heartbeat checks.
CheckResult evaluate_liveness();

} // namespace sentinel::health
