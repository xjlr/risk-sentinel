#include "sentinel/risk/rules/bridge_transfer_rule.hpp"
#include "sentinel/events/utils/hex.hpp"
#include <cstdio>
#include <cstring>
#include <string_view>

namespace sentinel::risk {

BridgeTransferRule::BridgeTransferRule(
    std::unordered_map<BridgeRuleKey, std::vector<BridgeRuleConfig>> configs_by_key,
    const std::unordered_set<BridgeAddressKey>& bridge_addresses,
    const std::unordered_map<BridgeAddressKey, std::string>& bridge_names)
    : configs_by_key_(std::move(configs_by_key))
    , bridge_addresses_(bridge_addresses)
    , bridge_names_(bridge_names) {}

SignalMask BridgeTransferRule::interests() const {
    return make_mask(SignalType::Transfer);
}

std::string_view BridgeTransferRule::rule_type_name() const {
    return "bridge_transfer";
}

void BridgeTransferRule::evaluate(const Signal& signal,
                                   StateStore& /* state_store */,
                                   std::vector<Alert>& out) {
    if (signal.type != SignalType::Transfer) {
        return;
    }

    const auto* evm = std::get_if<EvmLogEvent>(&signal.payload);
    if (!evm) {
        return;
    }

    if (evm->removed || evm->topic_count < 3 || evm->data_size < 32 || evm->truncated) {
        return;
    }

    // topics[2] is the `to` address, left-padded to 32 bytes; extract the last 20.
    std::array<uint8_t, 20> to_address{};
    std::copy(evm->topics[2].begin() + 12, evm->topics[2].end(), to_address.begin());

    BridgeAddressKey bridge_key{evm->chain_id, to_address};
    if (!bridge_addresses_.contains(bridge_key)) {
        return;
    }

    // O(1) lookup for the customer config bucket keyed by (chain_id, token).
    BridgeRuleKey rule_key{evm->chain_id, evm->address};
    auto bucket_it = configs_by_key_.find(rule_key);
    if (bucket_it == configs_by_key_.end()) {
        return;
    }

    // Resolve the bridge name once; avoid copying until we actually emit an alert.
    const std::string* bridge_name_ptr = nullptr;
    auto name_it = bridge_names_.find(bridge_key);
    if (name_it != bridge_names_.end()) {
        bridge_name_ptr = &name_it->second;
    }
    static constexpr std::string_view kUnknownBridge = "unknown bridge";
    static constexpr std::string_view kMsgPrefix = "Large transfer to bridge '";
    static constexpr std::string_view kMsgSuffix = "' detected";

    for (const auto& config : bucket_it->second) {
        if (!config.enabled) {
            continue;
        }
        if (!sentinel::events::utils::greater_be_256(evm->data.data(), config.threshold_be.data())) {
            continue;
        }

        std::string_view name = bridge_name_ptr
                                    ? std::string_view{*bridge_name_ptr}
                                    : kUnknownBridge;

        std::string msg;
        msg.reserve(kMsgPrefix.size() + name.size() + kMsgSuffix.size());
        msg.append(kMsgPrefix);
        msg.append(name);
        msg.append(kMsgSuffix);

        std::string amount_dec = sentinel::events::utils::uint256_be_to_decimal(evm->data.data());

        // TODO: replace the snprintf loop with a bytes_to_hex helper once one lands
        // in sentinel/events/utils/hex.hpp — no such helper exists today.
        std::string token_addr_str = "0x";
        for (uint8_t b : evm->address) {
            char buf[3];
            std::snprintf(buf, sizeof(buf), "%02x", b);
            token_addr_str += buf;
        }

        out.push_back(Alert{
            .customer_id    = config.customer_id,
            .rule_type      = "bridge_transfer",
            .message        = std::move(msg),
            .timestamp_ms   = signal.meta.timestamp_ms,
            .amount_decimal = std::move(amount_dec),
            .token_address  = std::move(token_addr_str),
            .chain_id       = config.chain_id,
        });
    }
}

} // namespace sentinel::risk
