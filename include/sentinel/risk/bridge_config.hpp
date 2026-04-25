#pragma once

#include <array>
#include <cstdint>
#include <functional>

namespace sentinel::risk {

struct BridgeRuleConfig {
    uint64_t customer_id;
    uint64_t chain_id;
    std::array<uint8_t, 20> token_address;
    std::array<uint8_t, 32> threshold_be;
    bool enabled;
};

struct BridgeRuleKey {
    uint64_t chain_id;
    std::array<uint8_t, 20> token_address;

    bool operator==(const BridgeRuleKey&) const = default;
};

struct BridgeAddressKey {
    uint64_t chain_id;
    std::array<uint8_t, 20> address;

    bool operator==(const BridgeAddressKey&) const = default;
};

} // namespace sentinel::risk

template <> struct std::hash<sentinel::risk::BridgeRuleKey> {
    std::size_t operator()(const sentinel::risk::BridgeRuleKey& k) const {
        std::size_t h = std::hash<uint64_t>()(k.chain_id);
        for (uint8_t b : k.token_address) {
            h ^= static_cast<std::size_t>(b) * 2654435761ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};

template <> struct std::hash<sentinel::risk::BridgeAddressKey> {
    std::size_t operator()(const sentinel::risk::BridgeAddressKey& k) const {
        std::size_t h = std::hash<uint64_t>()(k.chain_id);
        for (uint8_t b : k.address) {
            h ^= static_cast<std::size_t>(b) * 2654435761ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};
