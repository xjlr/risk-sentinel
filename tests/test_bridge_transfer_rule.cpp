#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/bridge_config.hpp"
#include "sentinel/risk/rules/bridge_transfer_rule.hpp"
#include "sentinel/risk/signal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstring>
#include <initializer_list>
#include <set>

using namespace sentinel::risk;
using namespace sentinel::events::utils;

// Build a Transfer signal (EvmLogEvent) for the bridge transfer rule.
// token_addr_hex   : ERC-20 contract address  (evm->address)
// to_addr_hex      : recipient               (topics[2] last 20 bytes)
// amount_dec       : transfer value in decimal
static Signal make_bridge_transfer_signal(
    uint64_t chain_id,
    const std::string& token_addr_hex,
    const std::string& to_addr_hex,
    const std::string& amount_dec,
    uint64_t timestamp_ms = 0,
    bool removed = false,
    uint8_t topic_count = 3) {

    Signal s{};
    s.type = SignalType::Transfer;
    s.meta.timestamp_ms = timestamp_ms;

    EvmLogEvent evm{};
    evm.chain_id = chain_id;
    evm.removed = removed;
    evm.topic_count = topic_count;
    evm.data_size = 32;
    evm.truncated = false;

    parse_hex_bytes(token_addr_hex, evm.address);

    // topics[2]: 32 bytes, first 12 are zero padding, last 20 are the address.
    evm.topics[2] = {};
    std::array<uint8_t, 20> to_addr{};
    parse_hex_bytes(to_addr_hex, to_addr);
    std::copy(to_addr.begin(), to_addr.end(), evm.topics[2].begin() + 12);

    auto amount_be = decimal_to_be_256(amount_dec);
    std::memcpy(evm.data.data(), amount_be.data(), 32);

    s.payload = evm;
    return s;
}

static BridgeRuleConfig make_config(
    uint64_t customer_id,
    uint64_t chain_id,
    const std::string& token_addr_hex,
    const std::string& threshold_dec,
    bool enabled = true) {

    BridgeRuleConfig cfg{};
    cfg.customer_id = customer_id;
    cfg.chain_id = chain_id;
    parse_hex_bytes(token_addr_hex, cfg.token_address);
    cfg.threshold_be = decimal_to_be_256(threshold_dec);
    cfg.enabled = enabled;
    return cfg;
}

static std::unordered_map<BridgeRuleKey, std::vector<BridgeRuleConfig>>
make_configs_map(std::initializer_list<BridgeRuleConfig> items) {
    std::unordered_map<BridgeRuleKey, std::vector<BridgeRuleConfig>> out;
    for (const auto& c : items) {
        out[BridgeRuleKey{c.chain_id, c.token_address}].push_back(c);
    }
    return out;
}

static const std::string kBridgeAddr = "0x53bf833a5d6c4ddd3ec4eb6e0e7e5c7e2c0e1a8b";
static const std::string kOtherAddr  = "0xdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef";
static const std::string kTokenAddr  = "0xfd086bc7cd5c481dcc9c85ebe478a1c0b69fcbb9";
static const std::string kToken2Addr = "0xff970a61a04b1ca14834a43f5de4533ebddb5cc8";
static const uint64_t    kChain      = 42161;

static std::pair<std::unordered_set<BridgeAddressKey>,
                 std::unordered_map<BridgeAddressKey, std::string>>
make_bridge_registry(const std::string& addr_hex, uint64_t chain_id,
                     const std::string& name = "Stargate") {
    std::unordered_set<BridgeAddressKey> addrs;
    std::unordered_map<BridgeAddressKey, std::string> names;

    std::array<uint8_t, 20> bytes{};
    parse_hex_bytes(addr_hex, bytes);
    BridgeAddressKey key{chain_id, bytes};
    addrs.insert(key);
    names[key] = name;

    return {std::move(addrs), std::move(names)};
}

TEST_CASE("BridgeTransferRule — rule skips non-Transfer signals") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain);
    auto configs = make_configs_map({make_config(1, kChain, kTokenAddr, "1000")});
    BridgeTransferRule rule(std::move(configs), addrs, names);

    Signal s{};
    s.type = SignalType::Governance;
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("BridgeTransferRule — Transfer to non-bridge address produces no alert") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain);
    auto configs = make_configs_map({make_config(1, kChain, kTokenAddr, "1000")});
    BridgeTransferRule rule(std::move(configs), addrs, names);

    // to_addr is NOT kBridgeAddr
    Signal s = make_bridge_transfer_signal(kChain, kTokenAddr, kOtherAddr, "999999");
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("BridgeTransferRule — Transfer to bridge, token not in customer config produces no alert") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain);
    // Config for kToken2Addr, but signal uses kTokenAddr
    auto configs = make_configs_map({make_config(1, kChain, kToken2Addr, "1000")});
    BridgeTransferRule rule(std::move(configs), addrs, names);

    Signal s = make_bridge_transfer_signal(kChain, kTokenAddr, kBridgeAddr, "999999");
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("BridgeTransferRule — Transfer to bridge, amount below threshold produces no alert") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain);
    auto configs = make_configs_map({make_config(1, kChain, kTokenAddr, "5000")});
    BridgeTransferRule rule(std::move(configs), addrs, names);

    // amount 4999 < threshold 5000
    Signal s = make_bridge_transfer_signal(kChain, kTokenAddr, kBridgeAddr, "4999");
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("BridgeTransferRule — Transfer to bridge above threshold fires alert with correct fields") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain, "Stargate");
    auto configs = make_configs_map({make_config(7, kChain, kTokenAddr, "1000", true)});
    BridgeTransferRule rule(std::move(configs), addrs, names);

    Signal s = make_bridge_transfer_signal(kChain, kTokenAddr, kBridgeAddr, "2000", 12345);
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);

    REQUIRE(alerts.size() == 1);
    const auto& a = alerts[0];
    REQUIRE(a.customer_id == 7);
    REQUIRE(a.rule_type == "bridge_transfer");
    REQUIRE(a.timestamp_ms == 12345);
    REQUIRE(a.message == "Large transfer to bridge 'Stargate' detected");
    REQUIRE(a.amount_decimal == "2000");
    REQUIRE(a.token_address == kTokenAddr);
    REQUIRE(a.chain_id == kChain);
}

TEST_CASE("BridgeTransferRule — two configs for same customer, different tokens; only matching fires") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain);
    auto configs = make_configs_map({
        make_config(1, kChain, kTokenAddr,  "1000"),
        make_config(1, kChain, kToken2Addr, "1000"),
    });
    BridgeTransferRule rule(std::move(configs), addrs, names);

    // Signal for kTokenAddr only
    Signal s = make_bridge_transfer_signal(kChain, kTokenAddr, kBridgeAddr, "9999");
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);

    REQUIRE(alerts.size() == 1);
    REQUIRE(alerts[0].token_address == kTokenAddr);
}

TEST_CASE("BridgeTransferRule — disabled config produces no alert") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain);
    auto configs = make_configs_map({make_config(1, kChain, kTokenAddr, "1000", /*enabled=*/false)});
    BridgeTransferRule rule(std::move(configs), addrs, names);

    Signal s = make_bridge_transfer_signal(kChain, kTokenAddr, kBridgeAddr, "999999");
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("BridgeTransferRule — removed=true signal produces no alert (reorg)") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain);
    auto configs = make_configs_map({make_config(1, kChain, kTokenAddr, "1000")});
    BridgeTransferRule rule(std::move(configs), addrs, names);

    Signal s = make_bridge_transfer_signal(kChain, kTokenAddr, kBridgeAddr, "999999",
                                           0, /*removed=*/true);
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("BridgeTransferRule — topic_count < 3 produces no alert (malformed)") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain);
    auto configs = make_configs_map({make_config(1, kChain, kTokenAddr, "1000")});
    BridgeTransferRule rule(std::move(configs), addrs, names);

    Signal s = make_bridge_transfer_signal(kChain, kTokenAddr, kBridgeAddr, "999999",
                                           0, false, /*topic_count=*/2);
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
}

TEST_CASE("BridgeTransferRule — two customers for same (chain, token) both fire") {
    auto [addrs, names] = make_bridge_registry(kBridgeAddr, kChain);
    auto configs = make_configs_map({
        make_config(1, kChain, kTokenAddr, "1000"),
        make_config(2, kChain, kTokenAddr, "500"),
    });
    BridgeTransferRule rule(std::move(configs), addrs, names);

    Signal s = make_bridge_transfer_signal(
        kChain, kTokenAddr, kBridgeAddr, "2000");
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);

    REQUIRE(alerts.size() == 2);

    // Customer ordering within a bucket is not guaranteed; check as a set.
    std::set<uint64_t> customer_ids;
    for (const auto& a : alerts) {
        customer_ids.insert(a.customer_id);
        REQUIRE(a.rule_type == "bridge_transfer");
        REQUIRE(a.token_address == kTokenAddr);
        REQUIRE(a.chain_id == kChain);
        REQUIRE(a.amount_decimal == "2000");
    }
    REQUIRE(customer_ids == std::set<uint64_t>{1, 2});
}
