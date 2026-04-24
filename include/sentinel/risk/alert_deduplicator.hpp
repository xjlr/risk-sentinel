#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace sentinel::risk {

struct Alert;

// Windows are in milliseconds. Keys not in the map use default_window_ms.
struct DeduplicatorConfig {
    uint64_t default_window_ms = 60'000;
    std::unordered_map<std::string, uint64_t> per_rule_window_ms;
    size_t cleanup_every_n_alerts = 100;
};

class AlertDeduplicator {
public:
    explicit AlertDeduplicator(DeduplicatorConfig cfg);

    // Returns true if this alert should be SUPPRESSED (i.e. a recent duplicate).
    // Returns false if the alert should be sent (and updates the last-fired
    // timestamp for this key).
    // now_ms is passed in for deterministic testing.
    bool should_suppress(const Alert& alert, uint64_t now_ms);

    // Removes entries older than the longest configured window.
    // Returns the number of entries removed.
    size_t cleanup_stale_entries(uint64_t now_ms);

    // Inspection (for tests/metrics): number of tracked keys.
    size_t tracked_keys_count() const { return last_fired_ms_.size(); }

private:
    DeduplicatorConfig cfg_;
    std::unordered_map<std::string, uint64_t> last_fired_ms_;
    size_t alerts_processed_since_cleanup_ = 0;
    uint64_t max_configured_window_ms_;  // computed once in constructor
};

} // namespace sentinel::risk
