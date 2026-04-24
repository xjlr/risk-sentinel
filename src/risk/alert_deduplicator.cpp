#include "sentinel/risk/alert_deduplicator.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"

#include <algorithm>
#include <string>

namespace sentinel::risk {

namespace {

// Format: "<customer_id>|<rule_type>|<chain_id>|<token_address>"
// chain_id uses "-" when std::nullopt; token_address uses "" when std::nullopt.
// "|" cannot appear in any component, so keys are unambiguous.
std::string build_key(const Alert& a) {
    std::string key;
    key.reserve(64);
    key += std::to_string(a.customer_id);
    key += '|';
    key += a.rule_type;
    key += '|';
    key += a.chain_id.has_value() ? std::to_string(*a.chain_id) : "-";
    key += '|';
    key += a.token_address.value_or("");
    return key;
}

} // namespace

AlertDeduplicator::AlertDeduplicator(DeduplicatorConfig cfg)
    : cfg_(std::move(cfg)), max_configured_window_ms_(cfg_.default_window_ms) {
    for (const auto& [rule_type, window] : cfg_.per_rule_window_ms) {
        max_configured_window_ms_ = std::max(max_configured_window_ms_, window);
    }
}

bool AlertDeduplicator::should_suppress(const Alert& alert, uint64_t now_ms) {
    const std::string key = build_key(alert);

    auto window_it = cfg_.per_rule_window_ms.find(alert.rule_type);
    const uint64_t window = (window_it != cfg_.per_rule_window_ms.end())
                                ? window_it->second
                                : cfg_.default_window_ms;

    bool suppress = false;
    auto map_it = last_fired_ms_.find(key);
    if (map_it == last_fired_ms_.end()) {
        last_fired_ms_.emplace(key, now_ms);
    } else if (now_ms < map_it->second || now_ms - map_it->second < window) {
        // Suppress if the clock went backwards (now_ms < stored) or if the
        // stored timestamp is still within the dedup window.
        suppress = true;
    } else {
        map_it->second = now_ms;
    }

    ++alerts_processed_since_cleanup_;
    if (alerts_processed_since_cleanup_ >= cfg_.cleanup_every_n_alerts) {
        cleanup_stale_entries(now_ms);
        alerts_processed_since_cleanup_ = 0;
    }

    return suppress;
}

size_t AlertDeduplicator::cleanup_stale_entries(uint64_t now_ms) {
    size_t removed = 0;
    std::erase_if(last_fired_ms_, [&](const auto& pair) {
        // If the clock went backwards keep the entry — we cannot determine age.
        if (now_ms < pair.second) return false;
        const bool stale = (now_ms - pair.second) > max_configured_window_ms_;
        if (stale) ++removed;
        return stale;
    });
    return removed;
}

} // namespace sentinel::risk
