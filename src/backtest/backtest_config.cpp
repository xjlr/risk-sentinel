#include "sentinel/backtest/backtest_config.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

#include <yaml-cpp/yaml.h>

#include "sentinel/events/utils/hex.hpp"
#include "sentinel/log.hpp"

namespace sentinel::backtest {

namespace {

void warn_unknown_keys(const YAML::Node &node,
                       const std::unordered_set<std::string> &known,
                       const std::string &context) {
  if (!node.IsMap()) return;
  auto &Lcore = sentinel::logger(sentinel::LogComponent::Core);
  for (auto it = node.begin(); it != node.end(); ++it) {
    const std::string key = it->first.as<std::string>();
    if (known.find(key) == known.end()) {
      Lcore.info("backtest config: ignoring unknown key '{}' in {}", key,
                 context);
    }
  }
}

void validate_customer_ids(const BacktestConfig &cfg) {
  std::unordered_set<std::uint64_t> declared;
  declared.reserve(cfg.customers.size());
  for (const auto &c : cfg.customers) {
    declared.insert(c.id);
  }

  auto check = [&declared](const std::string &rule_section,
                            std::uint64_t customer_id) {
    if (declared.find(customer_id) == declared.end()) {
      throw std::runtime_error(
          "backtest config: rule section '" + rule_section +
          "' references undeclared customer_id=" +
          std::to_string(customer_id) +
          "; add it to the customers section");
    }
  };

  for (const auto &c : cfg.large_transfer_configs) {
    check("large_transfer", c.customer_id);
  }
  for (const auto &[key, vec] : cfg.governance_rules_by_contract) {
    for (const auto &c : vec) check("governance", c.customer_id);
  }
  for (const auto &[key, vec] : cfg.mint_burn_rules_by_contract) {
    for (const auto &c : vec) check("mint_burn", c.customer_id);
  }
  for (const auto &[key, vec] : cfg.approval_rules_by_contract) {
    for (const auto &c : vec) check("approval", c.customer_id);
  }
  for (const auto &[key, vec] : cfg.bridge_configs_by_key) {
    for (const auto &c : vec) check("bridge_transfer", c.customer_id);
  }
  for (const auto &[key, vec] : cfg.oracle_configs_by_feed) {
    for (const auto &c : vec) check("oracle_update", c.customer_id);
  }
}

std::string lower(std::string s) {
  std::transform(s.begin(), s.end(), s.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return s;
}

std::string require_string(const YAML::Node &node, const char *field,
                           const std::string &context) {
  if (!node[field]) {
    throw std::runtime_error("backtest config: missing required field '" +
                             std::string(field) + "' in " + context);
  }
  try {
    return node[field].as<std::string>();
  } catch (const std::exception &e) {
    throw std::runtime_error(
        "backtest config: field '" + std::string(field) + "' in " + context +
        " is not a string: " + e.what());
  }
}

std::uint64_t require_uint(const YAML::Node &node, const char *field,
                           const std::string &context) {
  if (!node[field]) {
    throw std::runtime_error("backtest config: missing required field '" +
                             std::string(field) + "' in " + context);
  }
  try {
    return node[field].as<std::uint64_t>();
  } catch (const std::exception &e) {
    throw std::runtime_error(
        "backtest config: field '" + std::string(field) + "' in " + context +
        " is not an unsigned integer: " + e.what());
  }
}

int require_int(const YAML::Node &node, const char *field,
                const std::string &context) {
  if (!node[field]) {
    throw std::runtime_error("backtest config: missing required field '" +
                             std::string(field) + "' in " + context);
  }
  try {
    return node[field].as<int>();
  } catch (const std::exception &e) {
    throw std::runtime_error(
        "backtest config: field '" + std::string(field) + "' in " + context +
        " is not an integer: " + e.what());
  }
}

void validate_hex_address(const std::string &addr,
                          const std::string &context) {
  if (addr.size() != 42 || addr.substr(0, 2) != "0x") {
    throw std::runtime_error("backtest config: invalid hex address '" + addr +
                             "' (expected 0x + 40 hex chars) in " + context);
  }
  try {
    sentinel::events::utils::validate_hex(addr);
  } catch (const std::exception &e) {
    throw std::runtime_error("backtest config: invalid hex characters in '" +
                             addr + "' in " + context + ": " + e.what());
  }
}

void parse_large_transfer(const YAML::Node &items, BacktestConfig &cfg) {
  if (!items || items.IsNull()) return;
  if (!items.IsSequence()) {
    throw std::runtime_error(
        "backtest config: rules.large_transfer must be a sequence");
  }
  size_t idx = 0;
  for (const auto &item : items) {
    const std::string ctx =
        "rules.large_transfer[" + std::to_string(idx++) + "]";
    warn_unknown_keys(
        item,
        {"customer_id", "chain_id", "token_address", "threshold_decimal"},
        ctx);

    sentinel::risk::LargeTransferRuleConfig c{};
    c.customer_id = require_uint(item, "customer_id", ctx);
    c.chain_id = require_uint(item, "chain_id", ctx);
    const std::string token = lower(require_string(item, "token_address", ctx));
    validate_hex_address(token, ctx);
    sentinel::events::utils::parse_hex_bytes(token, c.token_address);
    const std::string thr_str = require_string(item, "threshold_decimal", ctx);
    c.threshold_be = sentinel::events::utils::decimal_to_be_256(thr_str);
    cfg.large_transfer_configs.push_back(c);
  }
}

void parse_governance(const YAML::Node &items, BacktestConfig &cfg) {
  if (!items || items.IsNull()) return;
  if (!items.IsSequence()) {
    throw std::runtime_error(
        "backtest config: rules.governance must be a sequence");
  }
  size_t idx = 0;
  for (const auto &item : items) {
    const std::string ctx = "rules.governance[" + std::to_string(idx++) + "]";
    warn_unknown_keys(item,
                      {"customer_id", "chain_id", "contract_address"}, ctx);

    sentinel::risk::GovernanceRuleConfig c{};
    c.customer_id = require_uint(item, "customer_id", ctx);
    c.chain_id = require_uint(item, "chain_id", ctx);
    c.contract_address =
        lower(require_string(item, "contract_address", ctx));
    validate_hex_address(c.contract_address, ctx);
    c.enabled = true;
    c.action_filter = std::nullopt;

    sentinel::risk::GovernanceContractKey key{c.chain_id, c.contract_address};
    cfg.governance_rules_by_contract[key].push_back(c);
  }
}

void parse_mint_burn(const YAML::Node &items, BacktestConfig &cfg) {
  if (!items || items.IsNull()) return;
  if (!items.IsSequence()) {
    throw std::runtime_error(
        "backtest config: rules.mint_burn must be a sequence");
  }
  size_t idx = 0;
  for (const auto &item : items) {
    const std::string ctx = "rules.mint_burn[" + std::to_string(idx++) + "]";
    warn_unknown_keys(
        item,
        {"customer_id", "chain_id", "contract_address",
         "mint_threshold_decimal", "burn_threshold_decimal"},
        ctx);

    sentinel::risk::MintBurnRuleConfig c{};
    c.customer_id = require_uint(item, "customer_id", ctx);
    c.chain_id = require_uint(item, "chain_id", ctx);
    c.contract_address =
        lower(require_string(item, "contract_address", ctx));
    validate_hex_address(c.contract_address, ctx);
    const std::string mint_thr =
        require_string(item, "mint_threshold_decimal", ctx);
    const std::string burn_thr =
        require_string(item, "burn_threshold_decimal", ctx);
    c.mint_threshold_be = sentinel::events::utils::decimal_to_be_256(mint_thr);
    c.burn_threshold_be = sentinel::events::utils::decimal_to_be_256(burn_thr);
    c.enabled = true;

    sentinel::risk::MintBurnContractKey key{c.chain_id, c.contract_address};
    cfg.mint_burn_rules_by_contract[key].push_back(c);
  }
}

void parse_approval(const YAML::Node &items, BacktestConfig &cfg) {
  if (!items || items.IsNull()) return;
  if (!items.IsSequence()) {
    throw std::runtime_error(
        "backtest config: rules.approval must be a sequence");
  }
  size_t idx = 0;
  for (const auto &item : items) {
    const std::string ctx = "rules.approval[" + std::to_string(idx++) + "]";
    warn_unknown_keys(item,
                      {"customer_id", "chain_id", "token_address",
                       "threshold_decimal", "alert_on_infinite"},
                      ctx);

    sentinel::risk::ApprovalRuleConfig c{};
    c.customer_id = require_uint(item, "customer_id", ctx);
    c.chain_id = require_uint(item, "chain_id", ctx);
    const std::string token = lower(require_string(item, "token_address", ctx));
    validate_hex_address(token, ctx);
    sentinel::events::utils::parse_hex_bytes(token, c.token_address);
    const std::string thr_str = require_string(item, "threshold_decimal", ctx);
    c.threshold_be = sentinel::events::utils::decimal_to_be_256(thr_str);
    c.alert_on_infinite = item["alert_on_infinite"]
                              ? item["alert_on_infinite"].as<bool>()
                              : false;
    c.enabled = true;

    sentinel::risk::ApprovalContractKey key{c.chain_id, token};
    cfg.approval_rules_by_contract[key].push_back(c);
  }
}

void parse_bridge_transfers(const YAML::Node &items, BacktestConfig &cfg) {
  if (!items || items.IsNull()) return;
  if (!items.IsSequence()) {
    throw std::runtime_error(
        "backtest config: rules.bridge_transfers must be a sequence");
  }
  size_t idx = 0;
  for (const auto &item : items) {
    const std::string ctx =
        "rules.bridge_transfers[" + std::to_string(idx++) + "]";
    warn_unknown_keys(
        item,
        {"customer_id", "chain_id", "token_address", "threshold_decimal"},
        ctx);

    sentinel::risk::BridgeRuleConfig c{};
    c.customer_id = require_uint(item, "customer_id", ctx);
    c.chain_id = require_uint(item, "chain_id", ctx);
    const std::string token = lower(require_string(item, "token_address", ctx));
    validate_hex_address(token, ctx);
    sentinel::events::utils::parse_hex_bytes(token, c.token_address);
    const std::string thr_str = require_string(item, "threshold_decimal", ctx);
    c.threshold_be = sentinel::events::utils::decimal_to_be_256(thr_str);
    c.enabled = true;

    sentinel::risk::BridgeRuleKey key{c.chain_id, c.token_address};
    cfg.bridge_configs_by_key[key].push_back(c);
  }
}

void parse_oracle_update(const YAML::Node &items, BacktestConfig &cfg) {
  if (!items || items.IsNull()) return;
  if (!items.IsSequence()) {
    throw std::runtime_error(
        "backtest config: rules.oracle_update must be a sequence");
  }
  size_t idx = 0;
  for (const auto &item : items) {
    const std::string ctx =
        "rules.oracle_update[" + std::to_string(idx++) + "]";
    warn_unknown_keys(item,
                      {"customer_id", "chain_id", "aggregator_address",
                       "feed_label", "spike_threshold_bps", "decimals"},
                      ctx);

    sentinel::risk::OracleRuleConfig c{};
    c.customer_id = require_uint(item, "customer_id", ctx);
    c.chain_id = require_uint(item, "chain_id", ctx);
    const std::string addr =
        lower(require_string(item, "aggregator_address", ctx));
    validate_hex_address(addr, ctx);
    sentinel::events::utils::parse_hex_bytes(addr, c.aggregator_address);
    c.feed_label = require_string(item, "feed_label", ctx);
    const int bps = require_int(item, "spike_threshold_bps", ctx);
    if (bps <= 0 || bps > 100000) {
      throw std::runtime_error(
          "backtest config: spike_threshold_bps out of range in " + ctx);
    }
    const int dec = require_int(item, "decimals", ctx);
    if (dec < 0 || dec > 255) {
      throw std::runtime_error(
          "backtest config: decimals out of range in " + ctx);
    }
    c.spike_threshold_bps = static_cast<std::uint32_t>(bps);
    c.decimals = static_cast<std::uint8_t>(dec);
    c.enabled = true;

    sentinel::risk::OracleFeedKey key{c.chain_id, c.aggregator_address};
    cfg.oracle_configs_by_feed[key].push_back(std::move(c));
  }
}

void parse_bridge_contracts(const YAML::Node &items, BacktestConfig &cfg) {
  if (!items || items.IsNull()) return;
  if (!items.IsSequence()) {
    throw std::runtime_error(
        "backtest config: bridge_contracts must be a sequence");
  }
  size_t idx = 0;
  for (const auto &item : items) {
    const std::string ctx = "bridge_contracts[" + std::to_string(idx++) + "]";
    warn_unknown_keys(item, {"chain_id", "address", "bridge_name"}, ctx);

    const std::uint64_t chain_id = require_uint(item, "chain_id", ctx);
    const std::string addr_str = lower(require_string(item, "address", ctx));
    validate_hex_address(addr_str, ctx);
    const std::string name = require_string(item, "bridge_name", ctx);

    std::array<std::uint8_t, 20> addr_bytes{};
    sentinel::events::utils::parse_hex_bytes(addr_str, addr_bytes);

    sentinel::risk::BridgeAddressKey key{chain_id, addr_bytes};
    cfg.bridge_addresses.insert(key);
    cfg.bridge_names[key] = name;
  }
}

void parse_customers(const YAML::Node &items, BacktestConfig &cfg) {
  if (!items || items.IsNull()) return;
  if (!items.IsSequence()) {
    throw std::runtime_error(
        "backtest config: customers must be a sequence");
  }
  size_t idx = 0;
  for (const auto &item : items) {
    const std::string ctx = "customers[" + std::to_string(idx++) + "]";
    warn_unknown_keys(item, {"id", "key", "display_name"}, ctx);

    CustomerEntry e{};
    e.id = require_uint(item, "id", ctx);
    e.key = require_string(item, "key", ctx);
    e.display_name = item["display_name"]
                         ? item["display_name"].as<std::string>()
                         : "";
    cfg.customers.push_back(std::move(e));
  }
}

} // namespace

BacktestConfig load_backtest_config(const std::string &yaml_path) {
  std::ifstream f(yaml_path);
  if (!f.is_open()) {
    throw std::runtime_error("backtest config: cannot open '" + yaml_path +
                             "'");
  }

  YAML::Node root;
  try {
    root = YAML::Load(f);
  } catch (const std::exception &e) {
    throw std::runtime_error("backtest config: YAML parse error in '" +
                             yaml_path + "': " + e.what());
  }
  if (!root || !root.IsMap()) {
    throw std::runtime_error("backtest config: top-level YAML must be a map in '" +
                             yaml_path + "'");
  }

  warn_unknown_keys(root,
                    {"chain", "start_block", "end_block", "output_path",
                     "max_block_range", "customers", "rules",
                     "bridge_contracts"},
                    "<root>");

  BacktestConfig cfg;

  cfg.chain = require_string(root, "chain", "<root>");
  if (cfg.chain != "ethereum" && cfg.chain != "arbitrum") {
    throw std::runtime_error(
        "backtest config: chain must be 'ethereum' or 'arbitrum', got '" +
        cfg.chain + "'");
  }

  cfg.start_block = require_uint(root, "start_block", "<root>");
  cfg.end_block = require_uint(root, "end_block", "<root>");
  if (cfg.start_block >= cfg.end_block) {
    throw std::runtime_error(
        "backtest config: start_block (" + std::to_string(cfg.start_block) +
        ") must be < end_block (" + std::to_string(cfg.end_block) + ")");
  }

  cfg.output_path = require_string(root, "output_path", "<root>");
  if (cfg.output_path.empty()) {
    throw std::runtime_error("backtest config: output_path must not be empty");
  }

  if (root["max_block_range"]) {
    cfg.max_block_range = root["max_block_range"].as<std::uint64_t>();
    if (cfg.max_block_range == 0) {
      throw std::runtime_error(
          "backtest config: max_block_range must be > 0");
    }
  }

  parse_customers(root["customers"], cfg);

  if (const auto rules = root["rules"]; rules && !rules.IsNull()) {
    if (!rules.IsMap()) {
      throw std::runtime_error("backtest config: rules must be a map");
    }
    warn_unknown_keys(rules,
                      {"large_transfer", "governance", "mint_burn", "approval",
                       "bridge_transfers", "oracle_update"},
                      "rules");

    parse_large_transfer(rules["large_transfer"], cfg);
    parse_governance(rules["governance"], cfg);
    parse_mint_burn(rules["mint_burn"], cfg);
    parse_approval(rules["approval"], cfg);
    parse_bridge_transfers(rules["bridge_transfers"], cfg);
    parse_oracle_update(rules["oracle_update"], cfg);
  }

  parse_bridge_contracts(root["bridge_contracts"], cfg);

  validate_customer_ids(cfg);

  return cfg;
}

} // namespace sentinel::backtest
