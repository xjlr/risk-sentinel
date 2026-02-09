#pragma once
#include <array>
#include <cstdint>

namespace sentinel::events {
struct NormalizedEvent {
    uint64_t chain_id;
    uint64_t block_number;
    uint64_t block_timestamp;

    uint32_t tx_index;
    uint32_t log_index;

    uint32_t data_size;
    uint8_t  topic_count;
    bool     removed;
    bool     truncated;

    std::array<uint8_t, 32> tx_hash;
    std::array<uint8_t, 20> contract;
    std::array<std::array<uint8_t, 32>, 4> topics;
    std::array<uint8_t, 256> data;
};
} //end of sentinel::events namespace
