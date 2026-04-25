#pragma once

#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/risk/bridge_config.hpp"
#include "sentinel/risk/rule_interface.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sentinel::risk {

class BridgeTransferRule : public IRiskRule {
public:
    BridgeTransferRule(
        std::unordered_map<BridgeRuleKey, std::vector<BridgeRuleConfig>> configs_by_key,
        const std::unordered_set<BridgeAddressKey>& bridge_addresses,
        const std::unordered_map<BridgeAddressKey, std::string>& bridge_names);

    SignalMask interests() const override;
    std::string_view rule_type_name() const override;

    void evaluate(const Signal& signal,
                  StateStore& state_store,
                  std::vector<Alert>& out) override;

private:
    std::unordered_map<BridgeRuleKey, std::vector<BridgeRuleConfig>> configs_by_key_;
    const std::unordered_set<BridgeAddressKey>& bridge_addresses_;
    const std::unordered_map<BridgeAddressKey, std::string>& bridge_names_;
};

} // namespace sentinel::risk
