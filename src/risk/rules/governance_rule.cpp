#include "sentinel/risk/rules/governance_rule.hpp"
#include "sentinel/events/utils/hex.hpp"
#include <cstdio>

namespace sentinel::risk {

namespace {

std::string action_to_string(GovernanceAction action) {
  switch (action) {
  case GovernanceAction::OwnershipTransferred:
    return "OwnershipTransferred";
  case GovernanceAction::Paused:
    return "Paused";
  case GovernanceAction::Unpaused:
    return "Unpaused";
  case GovernanceAction::RoleGranted:
    return "RoleGranted";
  case GovernanceAction::RoleRevoked:
    return "RoleRevoked";
  case GovernanceAction::Upgraded:
    return "Upgraded";
  case GovernanceAction::Unknown:
  default:
    return "Unknown";
  }
}

} // namespace

GovernanceRule::GovernanceRule(
    const std::unordered_map<GovernanceContractKey,
                             std::vector<GovernanceRuleConfig>> &config_map)
    : config_map_(config_map) {}

SignalMask GovernanceRule::interests() const {
  return make_mask(SignalType::Governance);
}

void GovernanceRule::evaluate(const Signal &signal,
                              StateStore & /* state_store */,
                              std::vector<Alert> &out) {
  if (signal.type != SignalType::Governance) {
    return; // Should not happen if dispatcher checks interests()
  }

  const auto *gov_event = std::get_if<GovernanceEvent>(&signal.payload);
  if (!gov_event) {
    return;
  }

  // Format the contract address to hex (lowercase) for lookup
  std::string contract_address_hex = "0x";
  for (uint8_t b : gov_event->contract_address) {
    char buf[3];
    std::snprintf(buf, sizeof(buf), "%02x", b);
    contract_address_hex += buf;
  }

  GovernanceContractKey key{gov_event->chain_id, contract_address_hex};

  auto it = config_map_.find(key);
  if (it == config_map_.end()) {
    return; // No customers listening to this contract
  }

  for (const auto &cfg : it->second) {
    if (!cfg.enabled) {
      continue;
    }

    // Check action filter if present
    if (cfg.action_filter.has_value() &&
        cfg.action_filter.value() != gov_event->action) {
      continue;
    }

    // Match! Create an alert.
    Alert alert{};
    alert.customer_id = cfg.customer_id;
    alert.rule_type = "governance";
    alert.timestamp_ms = signal.meta.timestamp_ms;
    alert.chain_id = gov_event->chain_id;
    alert.token_address = contract_address_hex;

    alert.message = std::string("Governance action '") + action_to_string(gov_event->action) +
                    "' detected on contract " + contract_address_hex;

    out.push_back(std::move(alert));
  }
}

} // namespace sentinel::risk
