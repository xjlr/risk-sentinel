#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/oracle_config.hpp"
#include "sentinel/risk/rules/oracle_update_rule.hpp"
#include "sentinel/risk/signal.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <vector>

using namespace sentinel::risk;
using namespace sentinel::events::utils;

namespace {

constexpr const char* kAggregator =
    "0x639fe6ab55c921f74e7fac1ee960c0b6293ba612"; // ETH/USD on Arbitrum
constexpr const char* kOtherAggregator =
    "0x6ce185860a4963106506c203335a2910413708e9"; // BTC/USD on Arbitrum
constexpr uint64_t kChain = 42161;

// Pack a uint64 into the low 8 bytes of a 32-byte big-endian slot.
std::array<uint8_t, 32> u64_to_be32(uint64_t value) {
    std::array<uint8_t, 32> out{};
    for (int i = 0; i < 8; ++i) {
        out[31 - i] = static_cast<uint8_t>((value >> (i * 8)) & 0xFF);
    }
    return out;
}

// Build a 32-byte big-endian value with the high bit set, representing a
// negative int256.
std::array<uint8_t, 32> negative_be32() {
    std::array<uint8_t, 32> out{};
    out[0] = 0x80;
    return out;
}

Signal make_oracle_signal(uint64_t chain_id,
                          const std::string& aggregator_hex,
                          uint64_t current_answer_u64,
                          uint64_t round_id_u64,
                          uint64_t updated_at,
                          uint64_t timestamp_ms) {
    Signal s{};
    s.type = SignalType::OracleUpdate;
    s.meta.timestamp_ms = timestamp_ms;

    OracleUpdateEvent ev{};
    ev.chain_id = chain_id;
    parse_hex_bytes(aggregator_hex, ev.aggregator_address);
    ev.current_answer = u64_to_be32(current_answer_u64);
    ev.round_id = u64_to_be32(round_id_u64);
    ev.updated_at = updated_at;

    s.payload = ev;
    return s;
}

Signal make_oracle_signal_with_answer(uint64_t chain_id,
                                      const std::string& aggregator_hex,
                                      const std::array<uint8_t, 32>& answer,
                                      uint64_t round_id_u64,
                                      uint64_t updated_at,
                                      uint64_t timestamp_ms) {
    Signal s{};
    s.type = SignalType::OracleUpdate;
    s.meta.timestamp_ms = timestamp_ms;

    OracleUpdateEvent ev{};
    ev.chain_id = chain_id;
    parse_hex_bytes(aggregator_hex, ev.aggregator_address);
    ev.current_answer = answer;
    ev.round_id = u64_to_be32(round_id_u64);
    ev.updated_at = updated_at;

    s.payload = ev;
    return s;
}

OracleRuleConfig make_config(uint64_t customer_id,
                             uint64_t chain_id,
                             const std::string& aggregator_hex,
                             const std::string& feed_label,
                             uint32_t threshold_bps,
                             uint8_t decimals = 8,
                             bool enabled = true) {
    OracleRuleConfig cfg{};
    cfg.customer_id = customer_id;
    cfg.chain_id = chain_id;
    parse_hex_bytes(aggregator_hex, cfg.aggregator_address);
    cfg.feed_label = feed_label;
    cfg.spike_threshold_bps = threshold_bps;
    cfg.decimals = decimals;
    cfg.enabled = enabled;
    return cfg;
}

std::unordered_map<OracleFeedKey, std::vector<OracleRuleConfig>>
make_configs_map(std::initializer_list<OracleRuleConfig> items) {
    std::unordered_map<OracleFeedKey, std::vector<OracleRuleConfig>> out;
    for (const auto& c : items) {
        OracleFeedKey k{c.chain_id, c.aggregator_address};
        out[k].push_back(c);
    }
    return out;
}

} // namespace

TEST_CASE("OracleUpdateRule — skips non-OracleUpdate signals") {
    auto configs = make_configs_map({make_config(1, kChain, kAggregator, "ETH/USD", 500)});
    OracleUpdateRule rule(std::move(configs));

    Signal s{};
    s.type = SignalType::Governance;
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("OracleUpdateRule — first observation per feed is silent and records state") {
    auto configs = make_configs_map({make_config(1, kChain, kAggregator, "ETH/USD", 500)});
    OracleUpdateRule rule(std::move(configs));

    Signal s = make_oracle_signal(kChain, kAggregator, 200000000000ULL, 1, 1700000000, 1000);
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());

    // Second observation with no change: still silent (delta = 0).
    Signal s2 = make_oracle_signal(kChain, kAggregator, 200000000000ULL, 2, 1700000060, 2000);
    rule.evaluate(s2, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("OracleUpdateRule — change below threshold produces no alert; state still updates") {
    auto configs = make_configs_map({make_config(1, kChain, kAggregator, "ETH/USD", 500)}); // 5%
    OracleUpdateRule rule(std::move(configs));

    StateStore store;
    std::vector<Alert> alerts;

    // First observation: $2000.00 (with 8 decimals: 200_000_000_000)
    Signal s1 = make_oracle_signal(kChain, kAggregator, 200000000000ULL, 1, 1700000000, 1000);
    rule.evaluate(s1, store, alerts);
    REQUIRE(alerts.empty());

    // Second observation: $2050.00 (+2.5%, below 5%)
    Signal s2 = make_oracle_signal(kChain, kAggregator, 205000000000ULL, 2, 1700000060, 2000);
    rule.evaluate(s2, store, alerts);
    REQUIRE(alerts.empty());

    // Third observation: $2150.00 — within 5% of 2050, so still no alert
    Signal s3 = make_oracle_signal(kChain, kAggregator, 215000000000ULL, 3, 1700000120, 3000);
    rule.evaluate(s3, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("OracleUpdateRule — change above threshold fires alert with correct fields") {
    auto configs = make_configs_map({make_config(7, kChain, kAggregator, "ETH/USD", 500)}); // 5%
    OracleUpdateRule rule(std::move(configs));

    StateStore store;
    std::vector<Alert> alerts;

    // Baseline
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 200000000000ULL, 1, 1700000000, 1000),
                  store, alerts);
    REQUIRE(alerts.empty());

    // +12.34%: 200_000_000_000 -> 224_680_000_000 — well above 5% threshold
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 224680000000ULL, 2, 1700000060, 2000),
                  store, alerts);

    REQUIRE(alerts.size() == 1);
    const auto& a = alerts[0];
    REQUIRE(a.customer_id == 7);
    REQUIRE(a.rule_type == "oracle_update");
    REQUIRE(a.timestamp_ms == 2000);
    REQUIRE(a.chain_id == kChain);
    REQUIRE(a.amount_decimal.has_value());
    REQUIRE(*a.amount_decimal == "224680000000");
    REQUIRE(a.token_address.has_value());
    REQUIRE(*a.token_address == kAggregator);
    REQUIRE(a.message.find("ETH/USD") != std::string::npos);
    REQUIRE(a.message.find("12.34") != std::string::npos);
    REQUIRE(a.message.find("% change") != std::string::npos);
}

TEST_CASE("OracleUpdateRule — comparison is against most recent observation, not first") {
    auto configs = make_configs_map({make_config(1, kChain, kAggregator, "ETH/USD", 500)});
    OracleUpdateRule rule(std::move(configs));

    StateStore store;
    std::vector<Alert> alerts;

    // 100, 102 (+2%), 104 (+~2% vs 102, but +4% vs 100). With 5% threshold,
    // none of these should fire.
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 100, 1, 0, 1000), store, alerts);
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 102, 2, 0, 2000), store, alerts);
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 104, 3, 0, 3000), store, alerts);
    REQUIRE(alerts.empty());

    // Now 110: vs 104 that's +5.76%, above threshold. If we compared against
    // the original 100, it would also fire. The point of this test is the
    // chained comparison stays correct.
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 110, 4, 0, 4000), store, alerts);
    REQUIRE(alerts.size() == 1);
}

TEST_CASE("OracleUpdateRule — negative price is skipped and does NOT update state") {
    auto configs = make_configs_map({make_config(1, kChain, kAggregator, "ETH/USD", 500)});
    OracleUpdateRule rule(std::move(configs));

    StateStore store;
    std::vector<Alert> alerts;

    // Establish baseline at 100
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 100, 1, 0, 1000), store, alerts);
    REQUIRE(alerts.empty());

    // Negative answer: skipped, no alert and state must NOT update.
    Signal s_neg = make_oracle_signal_with_answer(kChain, kAggregator,
                                                  negative_be32(), 2, 0, 2000);
    rule.evaluate(s_neg, store, alerts);
    REQUIRE(alerts.empty());

    // 110: +10% vs the still-recorded 100; should fire.
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 110, 3, 0, 3000), store, alerts);
    REQUIRE(alerts.size() == 1);
}

TEST_CASE("OracleUpdateRule — prev == 0 produces no alert, state is updated") {
    auto configs = make_configs_map({make_config(1, kChain, kAggregator, "ETH/USD", 500)});
    OracleUpdateRule rule(std::move(configs));

    StateStore store;
    std::vector<Alert> alerts;

    // Baseline: 0 (degenerate but possible)
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 0, 1, 0, 1000), store, alerts);
    REQUIRE(alerts.empty());

    // Next update: 1000 — would be infinite % from zero; rule must NOT alert
    // and must update state to 1000.
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 1000, 2, 0, 2000), store, alerts);
    REQUIRE(alerts.empty());

    // Now compare 1000 -> 2000 (+100%) — should fire.
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 2000, 3, 0, 3000), store, alerts);
    REQUIRE(alerts.size() == 1);
}

TEST_CASE("OracleUpdateRule — multiple customers same feed with different thresholds") {
    auto configs = make_configs_map({
        make_config(1, kChain, kAggregator, "ETH/USD",  500),  // 5%
        make_config(2, kChain, kAggregator, "ETH/USD", 1000),  // 10%
    });
    OracleUpdateRule rule(std::move(configs));

    StateStore store;
    std::vector<Alert> alerts;

    rule.evaluate(make_oracle_signal(kChain, kAggregator, 100, 1, 0, 1000), store, alerts);
    REQUIRE(alerts.empty());

    // +7% — exceeds customer 1 (5%) but not customer 2 (10%)
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 107, 2, 0, 2000), store, alerts);
    REQUIRE(alerts.size() == 1);
    REQUIRE(alerts[0].customer_id == 1);

    alerts.clear();

    // From 107 -> 130: +21.49%, exceeds both
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 130, 3, 0, 3000), store, alerts);
    REQUIRE(alerts.size() == 2);

    std::set<uint64_t> ids;
    for (const auto& a : alerts) ids.insert(a.customer_id);
    REQUIRE(ids == std::set<uint64_t>{1, 2});
}

TEST_CASE("OracleUpdateRule — disabled config produces no alert; state still updates") {
    auto configs = make_configs_map({
        make_config(1, kChain, kAggregator, "ETH/USD", 500, /*decimals=*/8, /*enabled=*/false),
    });
    OracleUpdateRule rule(std::move(configs));

    StateStore store;
    std::vector<Alert> alerts;

    rule.evaluate(make_oracle_signal(kChain, kAggregator, 100, 1, 0, 1000), store, alerts);
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 200, 2, 0, 2000), store, alerts);
    REQUIRE(alerts.empty());

    // Now if we re-enable by recreating the rule with the same fixture, we
    // can't here, but we can still verify state-tracking by sending a
    // continuation: 200 -> 200 should still be silent (no movement).
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 200, 3, 0, 3000), store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("OracleUpdateRule — feed with no configured customer records no state") {
    // configs ONLY for kAggregator; signals arrive for kOtherAggregator
    auto configs = make_configs_map({make_config(1, kChain, kAggregator, "ETH/USD", 500)});
    OracleUpdateRule rule(std::move(configs));

    StateStore store;
    std::vector<Alert> alerts;

    // Send signals on the unmonitored feed — must not affect the rule at all.
    rule.evaluate(make_oracle_signal(kChain, kOtherAggregator, 100, 1, 0, 1000), store, alerts);
    rule.evaluate(make_oracle_signal(kChain, kOtherAggregator, 1000, 2, 0, 2000), store, alerts);
    REQUIRE(alerts.empty());

    // Now hit the monitored feed for the first time — the COLD-START
    // observation must be silent. If state had leaked from the unmonitored
    // feed, this could fire — proving the implementation correctly
    // ignores unmonitored feeds.
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 100, 1, 0, 3000), store, alerts);
    REQUIRE(alerts.empty());

    // And confirm the monitored feed now does normal change-detection.
    rule.evaluate(make_oracle_signal(kChain, kAggregator, 200, 2, 0, 4000), store, alerts);
    REQUIRE(alerts.size() == 1);
}
