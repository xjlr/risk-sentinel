#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace sentinel::risk {

struct OracleRuleConfig {
    uint64_t customer_id;
    uint64_t chain_id;
    std::array<uint8_t, 20> aggregator_address;
    std::string feed_label;          // small, used in alert messages only
    uint32_t spike_threshold_bps;
    uint8_t decimals;
    bool enabled;
};

struct OracleFeedKey {
    uint64_t chain_id;
    std::array<uint8_t, 20> aggregator_address;

    bool operator==(const OracleFeedKey&) const = default;
};

} // namespace sentinel::risk

template <> struct std::hash<sentinel::risk::OracleFeedKey> {
    std::size_t operator()(const sentinel::risk::OracleFeedKey& k) const {
        std::size_t h = std::hash<uint64_t>()(k.chain_id);
        for (uint8_t b : k.aggregator_address) {
            h ^= static_cast<std::size_t>(b) * 2654435761ULL + 0x9e3779b9 + (h << 6) + (h >> 2);
        }
        return h;
    }
};
