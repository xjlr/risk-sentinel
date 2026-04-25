#pragma once

#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/risk/oracle_config.hpp"
#include "sentinel/risk/rule_interface.hpp"

#include <unordered_map>
#include <vector>

namespace sentinel::risk {

class OracleUpdateRule : public IRiskRule {
public:
    explicit OracleUpdateRule(
        std::unordered_map<OracleFeedKey, std::vector<OracleRuleConfig>> configs_by_feed);

    SignalMask interests() const override;
    std::string_view rule_type_name() const override;

    void evaluate(const Signal& signal,
                  StateStore& state_store,
                  std::vector<Alert>& out) override;

private:
    struct LastObservation {
        std::array<uint8_t, 32> answer;   // raw int256 big-endian
        uint64_t updated_at;
    };

    std::unordered_map<OracleFeedKey, std::vector<OracleRuleConfig>> configs_by_feed_;

    // Per-feed last observation. Only accessed from the RiskEngine thread,
    // so no locking is required.
    std::unordered_map<OracleFeedKey, LastObservation> last_by_feed_;
};

} // namespace sentinel::risk
