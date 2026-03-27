#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/rules/governance_rule.hpp"
#include "sentinel/risk/signal.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace sentinel::risk;
using namespace sentinel::events::utils;

TEST_CASE("Governance Rule Testing") {
  std::unordered_map<GovernanceContractKey, std::vector<GovernanceRuleConfig>> config_map;
  
  std::array<uint8_t, 20> token_addr{};
  parse_hex_bytes("0x1111111111111111111111111111111111111111", token_addr);

  // Setup config for contract 0x111...111 on chain 1
  GovernanceContractKey key{1, "0x1111111111111111111111111111111111111111"};
  
  // Customer 1: listens to all actions (no filter)
  config_map[key].push_back({
      .customer_id = 1,
      .chain_id = 1,
      .contract_address = "0x1111111111111111111111111111111111111111",
      .enabled = true,
      .action_filter = std::nullopt
  });

  // Customer 2: listens only to Paused
  config_map[key].push_back({
      .customer_id = 2,
      .chain_id = 1,
      .contract_address = "0x1111111111111111111111111111111111111111",
      .enabled = true,
      .action_filter = GovernanceAction::Paused
  });

  GovernanceRule rule(config_map);

  SECTION("Rule skips irrelevant signals (non-governance)") {
    Signal s;
    s.type = SignalType::Transfer;

    StateStore store;
    std::vector<Alert> alerts;

    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
  }

  SECTION("Rule matches and emits alerts for multiple customers (no filter)") {
    Signal s;
    s.type = SignalType::Governance;
    s.meta.timestamp_ms = 123456;

    GovernanceEvent gov{};
    gov.chain_id = 1;
    gov.contract_address = token_addr; // 0x11...11
    gov.action = GovernanceAction::OwnershipTransferred;
    s.payload = gov;

    StateStore store;
    std::vector<Alert> alerts;

    rule.evaluate(s, store, alerts);
    
    // Customer 1 (no filter) should match. Customer 2 (Paused only) should NOT match.
    REQUIRE(alerts.size() == 1);
    REQUIRE(alerts[0].customer_id == 1);
    REQUIRE(alerts[0].rule_type == "governance");
    REQUIRE(alerts[0].timestamp_ms == 123456);
    REQUIRE(alerts[0].chain_id == 1);
    REQUIRE(alerts[0].token_address == "0x1111111111111111111111111111111111111111");
    // Message should contain the action
    REQUIRE(alerts[0].message.find("OwnershipTransferred") != std::string::npos);
  }

  SECTION("Rule matches and emits alerts respecting filter") {
    Signal s;
    s.type = SignalType::Governance;
    s.meta.timestamp_ms = 123456;

    GovernanceEvent gov{};
    gov.chain_id = 1;
    gov.contract_address = token_addr;
    gov.action = GovernanceAction::Paused;
    s.payload = gov;

    StateStore store;
    std::vector<Alert> alerts;

    rule.evaluate(s, store, alerts);
    
    // Both Customer 1 and Customer 2 should match now.
    REQUIRE(alerts.size() == 2);
    
    bool has_c1 = false;
    bool has_c2 = false;
    for (const auto& a : alerts) {
      if (a.customer_id == 1) has_c1 = true;
      if (a.customer_id == 2) has_c2 = true;
      REQUIRE(a.rule_type == "governance");
      REQUIRE(a.message.find("Paused") != std::string::npos);
    }
    REQUIRE(has_c1);
    REQUIRE(has_c2);
  }

  SECTION("Rule skips if contract not in config") {
    Signal s;
    s.type = SignalType::Governance;
    
    std::array<uint8_t, 20> other_addr{};
    parse_hex_bytes("0x2222222222222222222222222222222222222222", other_addr);

    GovernanceEvent gov{};
    gov.chain_id = 1;
    gov.contract_address = other_addr;
    gov.action = GovernanceAction::OwnershipTransferred;
    s.payload = gov;

    StateStore store;
    std::vector<Alert> alerts;

    rule.evaluate(s, store, alerts);
    
    REQUIRE(alerts.empty());
  }
}
