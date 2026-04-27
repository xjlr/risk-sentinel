#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "sentinel/risk/approval_config.hpp"
#include "sentinel/risk/bridge_config.hpp"
#include "sentinel/risk/governance_config.hpp"
#include "sentinel/risk/mint_burn_config.hpp"
#include "sentinel/risk/oracle_config.hpp"
#include "sentinel/risk/rules/large_transfer_rule.hpp"

namespace sentinel::backtest {

struct CustomerEntry {
  std::uint64_t id = 0;
  std::string key;
  std::string display_name;
};

struct BacktestConfig {
  std::string chain;
  std::uint64_t start_block = 0;
  std::uint64_t end_block = 0;
  std::string output_path;
  std::uint64_t max_block_range = 1000;

  std::vector<CustomerEntry> customers;

  std::vector<sentinel::risk::LargeTransferRuleConfig> large_transfer_configs;
  std::unordered_map<sentinel::risk::GovernanceContractKey,
                     std::vector<sentinel::risk::GovernanceRuleConfig>>
      governance_rules_by_contract;
  std::unordered_map<sentinel::risk::MintBurnContractKey,
                     std::vector<sentinel::risk::MintBurnRuleConfig>>
      mint_burn_rules_by_contract;
  std::unordered_map<sentinel::risk::ApprovalContractKey,
                     std::vector<sentinel::risk::ApprovalRuleConfig>>
      approval_rules_by_contract;
  std::unordered_map<sentinel::risk::BridgeRuleKey,
                     std::vector<sentinel::risk::BridgeRuleConfig>>
      bridge_configs_by_key;
  std::unordered_set<sentinel::risk::BridgeAddressKey> bridge_addresses;
  std::unordered_map<sentinel::risk::BridgeAddressKey, std::string>
      bridge_names;
  std::unordered_map<sentinel::risk::OracleFeedKey,
                     std::vector<sentinel::risk::OracleRuleConfig>>
      oracle_configs_by_feed;
};

BacktestConfig load_backtest_config(const std::string &yaml_path);

} // namespace sentinel::backtest
