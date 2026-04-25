#include "sentinel/risk/rules/oracle_update_rule.hpp"

#include "sentinel/log.hpp"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <utility>

namespace sentinel::risk {

OracleUpdateRule::OracleUpdateRule(
    std::unordered_map<OracleFeedKey, std::vector<OracleRuleConfig>> configs_by_feed)
    : configs_by_feed_(std::move(configs_by_feed)) {}

SignalMask OracleUpdateRule::interests() const {
    return make_mask(SignalType::OracleUpdate);
}

std::string_view OracleUpdateRule::rule_type_name() const {
    return "oracle_update";
}

namespace {

inline std::string aggregator_to_hex(const std::array<uint8_t, 20>& addr) {
    std::string out = "0x";
    out.reserve(2 + 40);
    for (uint8_t b : addr) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        out.append(buf);
    }
    return out;
}

inline std::string format_pct_from_bps(uint64_t delta_bps) {
    // delta_bps / 100 = whole percent; delta_bps % 100 = hundredths.
    uint64_t whole = delta_bps / 100;
    uint64_t frac  = delta_bps % 100;
    std::string s = std::to_string(whole);
    s.push_back('.');
    if (frac < 10) s.push_back('0');
    s.append(std::to_string(frac));
    return s;
}

} // namespace

void OracleUpdateRule::evaluate(const Signal& signal,
                                StateStore& /* state_store */,
                                std::vector<Alert>& out) {
    if (signal.type != SignalType::OracleUpdate) {
        return;
    }

    const auto* oracle = std::get_if<OracleUpdateEvent>(&signal.payload);
    if (!oracle) {
        return;
    }

    OracleFeedKey key{oracle->chain_id, oracle->aggregator_address};

    // Only feeds that have at least one configured customer are tracked.
    // This bounds the size of last_by_feed_ to the configured set.
    auto cfg_it = configs_by_feed_.find(key);
    if (cfg_it == configs_by_feed_.end()) {
        return;
    }

    // Negative-price guard. Chainlink answers are int256; the high bit of the
    // big-endian representation is the sign. We don't compare negative prices
    // and we don't update state, so the next non-negative observation becomes
    // the new baseline.
    if (oracle->current_answer[0] & 0x80) {
        return;
    }

    // 64-bit fit guard: bytes [0..23] must all be zero for the value to fit
    // in uint64. Real production Chainlink answers always fit in int64.
    for (int i = 0; i < 24; ++i) {
        if (oracle->current_answer[i] != 0) {
            sentinel::logger(sentinel::LogComponent::Risk).debug(
                "OracleUpdateRule: skipping update with answer > 2^64 for "
                "chain_id={} aggregator={}",
                oracle->chain_id,
                aggregator_to_hex(oracle->aggregator_address));
            return;
        }
    }

    uint64_t current_u64 = 0;
    for (int i = 24; i < 32; ++i) {
        current_u64 = (current_u64 << 8) | oracle->current_answer[i];
    }

    auto state_it = last_by_feed_.find(key);
    if (state_it == last_by_feed_.end()) {
        // Cold start for this feed: record and emit no alert.
        last_by_feed_.emplace(key, LastObservation{
            oracle->current_answer,
            oracle->updated_at,
        });
        return;
    }

    const LastObservation& last = state_it->second;

    uint64_t prev_u64 = 0;
    for (int i = 24; i < 32; ++i) {
        prev_u64 = (prev_u64 << 8) | last.answer[i];
    }

    if (prev_u64 == 0) {
        // Cannot compute a percentage from a zero baseline. Update state and
        // skip alerting; the next observation will compare against this one.
        state_it->second = LastObservation{
            oracle->current_answer,
            oracle->updated_at,
        };
        return;
    }

    uint64_t diff = (current_u64 > prev_u64)
                        ? (current_u64 - prev_u64)
                        : (prev_u64 - current_u64);

    // delta_bps = diff * 10000 / prev_u64. The intermediate `diff * 10000`
    // can overflow uint64 for very large prev/diff, so widen to __int128.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
    using i128 = __int128;
    i128 delta_bps_i = (static_cast<i128>(diff) * 10000) / prev_u64;
    uint64_t delta_bps = (delta_bps_i > static_cast<i128>(UINT64_MAX))
                             ? UINT64_MAX
                             : static_cast<uint64_t>(delta_bps_i);
#pragma GCC diagnostic pop

    std::optional<std::string> aggregator_hex;

    for (const auto& cfg : cfg_it->second) {
        if (!cfg.enabled) {
            continue;
        }
        if (delta_bps <= cfg.spike_threshold_bps) {
            continue;
        }

        // Build the hex address lazily on the first emitted alert; share
        // across multiple alerts in this bucket without re-formatting.
        if (!aggregator_hex.has_value()) {
            aggregator_hex = aggregator_to_hex(oracle->aggregator_address);
        }

        std::string pct = format_pct_from_bps(delta_bps);

        std::string msg;
        msg.reserve(32 + cfg.feed_label.size() + pct.size());
        msg.append("Oracle spike on ");
        msg.append(cfg.feed_label);
        msg.append(": ");
        msg.append(pct);
        msg.append("% change");

        Alert alert{};
        alert.customer_id    = cfg.customer_id;
        alert.rule_type      = "oracle_update";
        alert.message        = std::move(msg);
        alert.timestamp_ms   = signal.meta.timestamp_ms;
        alert.amount_decimal = std::to_string(current_u64);
        alert.token_address  = *aggregator_hex;
        alert.chain_id       = oracle->chain_id;
        out.push_back(std::move(alert));
    }

    // Always update state, even if no alert fired (or all configs disabled).
    // The next update will compare against this observation.
    state_it->second = LastObservation{
        oracle->current_answer,
        oracle->updated_at,
    };
}

} // namespace sentinel::risk
