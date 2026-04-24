#include "sentinel/health/health_checks.hpp"

#include <chrono>
#include <string>

namespace sentinel::health {

namespace {

uint64_t steady_now_ms() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

uint64_t system_now_s() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

} // namespace

ReadinessReport evaluate_readiness(const HealthCheckInputs& inputs,
                                   const HealthChecksConfig& cfg) {
    ReadinessReport report;
    report.ready = true;

    const uint64_t now_ms = steady_now_ms();

    // 1. threads_alive: all three heartbeats recorded within heartbeat_max_age_ms
    {
        CheckResult r{true, ""};
        auto check_hb = [&](const Heartbeat* hb, const char* name) {
            if (!hb) {
                r.ok = false;
                r.detail += std::string(name) + " heartbeat missing; ";
                return;
            }
            uint64_t age = hb->age_ms(now_ms);
            if (age > cfg.heartbeat_max_age_ms) {
                r.ok = false;
                r.detail += std::string(name) + " stale (" + std::to_string(age) + "ms); ";
            }
        };
        check_hb(inputs.event_source, "event_source");
        check_hb(inputs.risk_engine,  "risk_engine");
        check_hb(inputs.dispatcher,   "dispatcher");
        if (!r.detail.empty() && r.detail.back() == ' ') r.detail.pop_back();
        report.checks["threads_alive"] = r;
        if (!r.ok) report.ready = false;
    }

    // 2. db_connected: SELECT 1 probe
    {
        CheckResult r{false, ""};
        if (inputs.db_probe) {
            r.ok = inputs.db_probe();
        }
        if (!r.ok) r.detail = "DB probe failed";
        report.checks["db_connected"] = r;
        if (!r.ok) report.ready = false;
    }

    // 3. rpc_recent: last RPC success within rpc_max_silence_ms
    //    If last_s == 0, we are still in startup — report ok to avoid flapping.
    {
        CheckResult r{true, ""};
        if (inputs.rpc_last_success_unix_s) {
            uint64_t last_s = inputs.rpc_last_success_unix_s();
            if (last_s == 0) {
                r.detail = "no RPC calls yet";
            } else {
                uint64_t now_s = system_now_s();
                uint64_t silence_s = (now_s >= last_s) ? (now_s - last_s) : 0;
                uint64_t max_silence_s = cfg.rpc_max_silence_ms / 1000;
                if (silence_s >= max_silence_s) {
                    r.ok = false;
                    r.detail = "last RPC success " + std::to_string(silence_s) + "s ago";
                }
            }
        }
        report.checks["rpc_recent"] = r;
        if (!r.ok) report.ready = false;
    }

    // 4. heartbeats_recorded: all three have run at least once (last_ms > 0)
    {
        CheckResult r{true, ""};
        auto check_recorded = [&](const Heartbeat* hb, const char* name) {
            if (!hb || hb->last_ms() == 0) {
                r.ok = false;
                r.detail += std::string(name) + " not yet recorded; ";
            }
        };
        check_recorded(inputs.event_source, "event_source");
        check_recorded(inputs.risk_engine,  "risk_engine");
        check_recorded(inputs.dispatcher,   "dispatcher");
        if (!r.detail.empty() && r.detail.back() == ' ') r.detail.pop_back();
        report.checks["heartbeats_recorded"] = r;
        if (!r.ok) report.ready = false;
    }

    return report;
}

CheckResult evaluate_liveness() {
    return {true, ""};
}

} // namespace sentinel::health
