#pragma once

#include "sentinel/risk/rule_interface.hpp"
#include "sentinel/risk/mint_burn_config.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"

#include <unordered_map>
#include <vector>

namespace sentinel::risk {

class MintBurnRule : public IRiskRule {
public:
  MintBurnRule(
      const std::unordered_map<MintBurnContractKey,
                               std::vector<MintBurnRuleConfig>> &config_map);

  SignalMask interests() const override;
  std::string_view rule_type_name() const override;

  void evaluate(const Signal &signal, StateStore &state_store,
                std::vector<Alert> &out) override;

private:
  const std::unordered_map<MintBurnContractKey,
                           std::vector<MintBurnRuleConfig>> &config_map_;
};

} // namespace sentinel::risk
