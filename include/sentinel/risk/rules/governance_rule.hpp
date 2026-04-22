#pragma once

#include "sentinel/risk/rule_interface.hpp"
#include "sentinel/risk/governance_config.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"

#include <unordered_map>
#include <vector>

namespace sentinel::risk {

class GovernanceRule : public IRiskRule {
public:
  explicit GovernanceRule(
      const std::unordered_map<GovernanceContractKey,
                               std::vector<GovernanceRuleConfig>> &config_map);

  SignalMask interests() const override;
  std::string_view rule_type_name() const override;

  void evaluate(const Signal &signal, StateStore &state_store,
                std::vector<Alert> &out) override;

private:
  // Store a const reference to the app's loaded config map.
  const std::unordered_map<GovernanceContractKey,
                           std::vector<GovernanceRuleConfig>> &config_map_;
};

} // namespace sentinel::risk
