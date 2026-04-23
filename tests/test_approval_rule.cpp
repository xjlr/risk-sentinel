#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/rules/approval_rule.hpp"
#include "sentinel/risk/signal.hpp"
#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstring>

using namespace sentinel::risk;
using namespace sentinel::events::utils;

static Signal make_approval_signal(uint64_t chain_id,
                                   const std::string &token_hex,
                                   const std::array<uint8_t, 32> &amount,
                                   uint64_t timestamp_ms = 0,
                                   bool removed = false) {
    Signal s{};
    s.type = SignalType::Approval;
    s.meta.timestamp_ms = timestamp_ms;

    EvmLogEvent evm{};
    evm.chain_id = chain_id;
    evm.removed = removed;
    evm.topic_count = 3;
    evm.data_size = 32;
    evm.truncated = false;
    parse_hex_bytes(token_hex, evm.address);
    std::memcpy(evm.data.data(), amount.data(), 32);

    s.payload = evm;
    return s;
}

TEST_CASE("Approval Rule Testing") {
    std::unordered_map<ApprovalContractKey, std::vector<ApprovalRuleConfig>> configs;

    std::string token_addr_str = "0x1234567890123456789012345678901234567890";
    ApprovalContractKey key{1, token_addr_str};

    ApprovalRuleConfig cfg{};
    cfg.customer_id = 42;
    cfg.chain_id = 1;
    parse_hex_bytes(token_addr_str, cfg.token_address);
    cfg.threshold_be = decimal_to_be_256("1000");
    cfg.alert_on_infinite = true;
    cfg.enabled = true;
    configs[key].push_back(cfg);

    ApprovalRule rule(configs);

    SECTION("Rule skips non-Approval signals") {
        Signal s{};
        s.type = SignalType::Transfer;
        StateStore store;
        std::vector<Alert> alerts;
        rule.evaluate(s, store, alerts);
        REQUIRE(alerts.empty());
    }

    SECTION("Large approval above threshold triggers alert") {
        auto amount = decimal_to_be_256("2000");
        Signal s = make_approval_signal(1, token_addr_str, amount, 9999);

        StateStore store;
        std::vector<Alert> alerts;
        rule.evaluate(s, store, alerts);

        REQUIRE(alerts.size() == 1);
        REQUIRE(alerts[0].customer_id == 42);
        REQUIRE(alerts[0].rule_type == "approval");
        REQUIRE(alerts[0].message == "Large approval detected");
        REQUIRE(alerts[0].timestamp_ms == 9999);
        REQUIRE(alerts[0].amount_decimal == "2000");
        REQUIRE(alerts[0].token_address == token_addr_str);
    }

    SECTION("Large approval below threshold does not trigger alert") {
        auto amount = decimal_to_be_256("500");
        Signal s = make_approval_signal(1, token_addr_str, amount);

        StateStore store;
        std::vector<Alert> alerts;
        rule.evaluate(s, store, alerts);

        REQUIRE(alerts.empty());
    }

    SECTION("Infinite approval triggers alert when alert_on_infinite = true") {
        std::array<uint8_t, 32> infinite_amount;
        infinite_amount.fill(0xFF);
        Signal s = make_approval_signal(1, token_addr_str, infinite_amount);

        StateStore store;
        std::vector<Alert> alerts;
        rule.evaluate(s, store, alerts);

        REQUIRE(alerts.size() == 1);
        REQUIRE(alerts[0].message == "Infinite approval detected");
    }

    SECTION("Infinite approval does NOT trigger when alert_on_infinite = false and amount below threshold") {
        std::unordered_map<ApprovalContractKey, std::vector<ApprovalRuleConfig>> no_inf_configs;
        ApprovalRuleConfig no_inf_cfg{};
        no_inf_cfg.customer_id = 1;
        no_inf_cfg.chain_id = 1;
        parse_hex_bytes(token_addr_str, no_inf_cfg.token_address);
        // Set threshold above uint256 max — impossible to exceed normally,
        // but all-0xFF IS the max, so we use threshold = all-0xFF+1 isn't possible.
        // Instead use a large threshold so amount (all-0xFF) would exceed it,
        // but we want to test the alert_on_infinite=false path independently.
        // Use threshold = all-0xFF (equal, not greater), so exceeds = false.
        no_inf_cfg.threshold_be.fill(0xFF);
        no_inf_cfg.alert_on_infinite = false;
        no_inf_cfg.enabled = true;
        no_inf_configs[key].push_back(no_inf_cfg);

        ApprovalRule no_inf_rule(no_inf_configs);

        std::array<uint8_t, 32> infinite_amount;
        infinite_amount.fill(0xFF);
        Signal s = make_approval_signal(1, token_addr_str, infinite_amount);

        StateStore store;
        std::vector<Alert> alerts;
        no_inf_rule.evaluate(s, store, alerts);

        REQUIRE(alerts.empty());
    }

    SECTION("Token address not in config produces no alert") {
        auto amount = decimal_to_be_256("9999999");
        std::string other_token = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
        Signal s = make_approval_signal(1, other_token, amount);

        StateStore store;
        std::vector<Alert> alerts;
        rule.evaluate(s, store, alerts);

        REQUIRE(alerts.empty());
    }
}
