#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace sentinel::risk {

struct ApprovalRuleConfig {
    uint64_t customer_id;
    uint64_t chain_id;
    std::array<uint8_t, 20> token_address;
    std::array<uint8_t, 32> threshold_be;
    bool alert_on_infinite;
    bool enabled;
};

struct ApprovalContractKey {
    uint64_t chain_id;
    std::string token_address;   // lowercase hex with 0x prefix

    bool operator==(const ApprovalContractKey &other) const {
        return chain_id == other.chain_id &&
               token_address == other.token_address;
    }
};

} // namespace sentinel::risk

template <> struct std::hash<sentinel::risk::ApprovalContractKey> {
    std::size_t
    operator()(const sentinel::risk::ApprovalContractKey &key) const {
        return std::hash<uint64_t>()(key.chain_id) ^
               (std::hash<std::string>()(key.token_address) << 1);
    }
};
