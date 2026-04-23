#include "sentinel/risk/rules/approval_rule.hpp"
#include "sentinel/events/utils/hex.hpp"
#include <algorithm>
#include <cstdio>

namespace sentinel::risk {

ApprovalRule::ApprovalRule(
    const std::unordered_map<ApprovalContractKey,
                             std::vector<ApprovalRuleConfig>> &config_map)
    : config_map_(config_map) {}

SignalMask ApprovalRule::interests() const {
    return make_mask(SignalType::Approval);
}

std::string_view ApprovalRule::rule_type_name() const { return "approval"; }

void ApprovalRule::evaluate(const Signal &signal,
                             StateStore & /* state_store */,
                             std::vector<Alert> &out) {
    if (signal.type != SignalType::Approval) {
        return;
    }

    const auto *evm = std::get_if<EvmLogEvent>(&signal.payload);
    if (!evm || evm->removed || evm->topic_count < 3 ||
        evm->data_size < 32 || evm->truncated) {
        return;
    }

    std::string token_address_hex = "0x";
    for (uint8_t b : evm->address) {
        char buf[3];
        std::snprintf(buf, sizeof(buf), "%02x", b);
        token_address_hex += buf;
    }

    ApprovalContractKey key{evm->chain_id, token_address_hex};
    auto it = config_map_.find(key);
    if (it == config_map_.end()) {
        return;
    }

    bool is_infinite = std::all_of(evm->data.begin(), evm->data.begin() + 32,
                                   [](uint8_t b) { return b == 0xFF; });

    for (const auto &cfg : it->second) {
        if (!cfg.enabled) {
            continue;
        }

        bool exceeds = sentinel::events::utils::greater_be_256(
            evm->data.data(), cfg.threshold_be.data());
        bool is_alert = (cfg.alert_on_infinite && is_infinite) || exceeds;

        if (!is_alert) {
            continue;
        }

        Alert alert{};
        alert.customer_id = cfg.customer_id;
        alert.rule_type = "approval";
        alert.timestamp_ms = signal.meta.timestamp_ms;
        alert.chain_id = evm->chain_id;
        alert.token_address = token_address_hex;
        alert.amount_decimal =
            sentinel::events::utils::uint256_be_to_decimal(evm->data.data());
        alert.message = is_infinite ? "Infinite approval detected"
                                    : "Large approval detected";

        out.push_back(std::move(alert));
    }
}

} // namespace sentinel::risk
