#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/rules/mint_burn_rule.hpp"
#include "sentinel/risk/signal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <cstring>

using namespace sentinel::risk;
using namespace sentinel::events::utils;

TEST_CASE("MintBurn Rule Testing") {
  std::unordered_map<MintBurnContractKey, std::vector<MintBurnRuleConfig>> configs;
  
  std::string token_addr_str = "0x1234567890123456789012345678901234567890";
  MintBurnContractKey key{1, token_addr_str};

  MintBurnRuleConfig cfg;
  cfg.customer_id = 99;
  cfg.chain_id = 1;
  cfg.contract_address = token_addr_str;
  cfg.mint_threshold_be = decimal_to_be_256("1000");   // alert if mint >= 1000
  cfg.burn_threshold_be = decimal_to_be_256("2000");   // alert if burn >= 2000
  cfg.enabled = true;
  configs[key].push_back(cfg);

  MintBurnRule rule(configs);

  SECTION("Rule skips irrelevant signals") {
    Signal s;
    s.type = SignalType::Unknown;
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
  }

  SECTION("Mint event above threshold triggers alert") {
    Signal s;
    s.type = SignalType::MintBurn;
    s.meta.timestamp_ms = 5000;
    
    MintBurnEvent e;
    e.direction = MintBurnDirection::Mint;
    e.chain_id = 1;
    parse_hex_bytes(token_addr_str, e.token_address);
    std::memcpy(e.amount.data(), decimal_to_be_256("1500").data(), 32); 

    s.payload = e;

    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);

    REQUIRE(alerts.size() == 1);
    REQUIRE(alerts[0].customer_id == 99);
    REQUIRE(alerts[0].rule_type == "mint_burn");
    REQUIRE(alerts[0].timestamp_ms == 5000);
    REQUIRE(alerts[0].message == "Large Mint detected");
    REQUIRE(alerts[0].amount_decimal == "1500");
  }

  SECTION("Mint event below threshold does not trigger alert") {
    Signal s;
    s.type = SignalType::MintBurn;
    
    MintBurnEvent e;
    e.direction = MintBurnDirection::Mint;
    e.chain_id = 1;
    parse_hex_bytes(token_addr_str, e.token_address);
    std::memcpy(e.amount.data(), decimal_to_be_256("500").data(), 32); 

    s.payload = e;
    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);

    REQUIRE(alerts.empty());
  }

  SECTION("Burn event above threshold triggers alert") {
    Signal s;
    s.type = SignalType::MintBurn;
    
    MintBurnEvent e;
    e.direction = MintBurnDirection::Burn;
    e.chain_id = 1;
    parse_hex_bytes(token_addr_str, e.token_address);
    std::memcpy(e.amount.data(), decimal_to_be_256("2500").data(), 32); 

    s.payload = e;

    StateStore store;
    std::vector<Alert> alerts;
    rule.evaluate(s, store, alerts);

    REQUIRE(alerts.size() == 1);
    REQUIRE(alerts[0].message == "Large Burn detected");
  }
}
