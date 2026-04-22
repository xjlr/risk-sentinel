#include "sentinel/risk/rules/mint_burn_rule.hpp"
#include "sentinel/events/utils/hex.hpp"
#include <cstdio>
#include <cstring>

namespace sentinel::risk {

MintBurnRule::MintBurnRule(
    const std::unordered_map<MintBurnContractKey,
                             std::vector<MintBurnRuleConfig>> &config_map)
    : config_map_(config_map) {}

SignalMask MintBurnRule::interests() const {
  return make_mask(SignalType::MintBurn);
}

std::string_view MintBurnRule::rule_type_name() const { return "mint_burn"; }

void MintBurnRule::evaluate(const Signal &signal, StateStore & /* state_store */,
                            std::vector<Alert> &out) {
  if (signal.type != SignalType::MintBurn) {
    return;
  }

  const auto *mb_event = std::get_if<MintBurnEvent>(&signal.payload);
  if (!mb_event) {
    return;
  }

  std::string contract_address_hex = "0x";
  for (uint8_t b : mb_event->token_address) {
    char buf[3];
    std::snprintf(buf, sizeof(buf), "%02x", b);
    contract_address_hex += buf;
  }

  MintBurnContractKey key{mb_event->chain_id, contract_address_hex};
  auto it = config_map_.find(key);
  if (it == config_map_.end()) {
    return;
  }

  for (const auto &cfg : it->second) {
    if (!cfg.enabled) {
      continue;
    }

    bool is_alert = false;
    if (mb_event->direction == MintBurnDirection::Mint) {
      is_alert = !sentinel::events::utils::greater_be_256(cfg.mint_threshold_be.data(), mb_event->amount.data());
    } else if (mb_event->direction == MintBurnDirection::Burn) {
      is_alert = !sentinel::events::utils::greater_be_256(cfg.burn_threshold_be.data(), mb_event->amount.data());
    }

    if (is_alert) {
      Alert alert{};
      alert.customer_id = cfg.customer_id;
      alert.rule_type = "mint_burn";
      alert.timestamp_ms = signal.meta.timestamp_ms;
      alert.chain_id = mb_event->chain_id;
      alert.token_address = contract_address_hex;
      alert.amount_decimal = sentinel::events::utils::uint256_be_to_decimal(mb_event->amount.data());

      std::string dir_str = mb_event->direction == MintBurnDirection::Mint ? "Mint" : "Burn";
      alert.message = "Large " + dir_str + " detected";
      
      out.push_back(std::move(alert));
    }
  }
}

} // namespace sentinel::risk
