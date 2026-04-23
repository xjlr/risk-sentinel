#pragma once

#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/risk/approval_config.hpp"
#include "sentinel/risk/rule_interface.hpp"

#include <unordered_map>
#include <vector>

namespace sentinel::risk {

class ApprovalRule : public IRiskRule {
public:
    ApprovalRule(
        const std::unordered_map<ApprovalContractKey,
                                 std::vector<ApprovalRuleConfig>> &config_map);

    SignalMask interests() const override;
    std::string_view rule_type_name() const override;

    void evaluate(const Signal &signal, StateStore &state_store,
                  std::vector<Alert> &out) override;

private:
    const std::unordered_map<ApprovalContractKey,
                             std::vector<ApprovalRuleConfig>> &config_map_;
};

} // namespace sentinel::risk
