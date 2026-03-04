#include "sentinel/events/normalize.hpp"
#include "sentinel/events/utils/hex.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace sentinel::events {

void normalize(const RawLog& raw,
               sentinel::risk::Signal& out,
               uint64_t chain_id,
               uint64_t block_timestamp)
{
    out = sentinel::risk::Signal{}; // safe zero-init
    
    // Convert RawLog topics to check semantics. For now, we set a default type
    // and rely on a normalizer mapping in the future.
    // The spec says: topic0 == SWAP_TOPIC -> SignalType::Swap
    // Here we will just assign SignalType::Swap as a placeholder if topic counts > 0.
    // In a real system, we'd have a mapping.
    out.type = sentinel::risk::SignalType::Swap;

    // Meta (hex -> integers)
    out.meta.timestamp_ms = block_timestamp;
    out.meta.block_number = utils::parse_hex_uint64(raw.blockNumber);
    out.meta.is_final = false; // By default; Reorg logic happens before/elsewhere
    out.meta.source_id = static_cast<uint32_t>(chain_id);
    std::array<uint8_t, 32> tx_hash;
    utils::parse_hex_bytes(raw.transactionHash, tx_hash);
    out.meta.tx_hash = tx_hash;

    // Payload: EvmLogEvent
    sentinel::risk::EvmLogEvent evm{};
    evm.chain_id = chain_id;
    
    const uint64_t txi = utils::parse_hex_uint64(raw.transactionIndex);
    const uint64_t lgi = utils::parse_hex_uint64(raw.logIndex);

    if (txi > std::numeric_limits<uint32_t>::max() ||
        lgi > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("tx_index/log_index out of range");
    }

    evm.tx_index  = static_cast<uint32_t>(txi);
    evm.log_index = static_cast<uint32_t>(lgi);
    evm.removed = raw.removed;

    // Address
    utils::parse_hex_bytes(raw.address, evm.address);

    // Topics (max 4)
    const std::size_t tc = std::min<std::size_t>(raw.topics.size(), 4);
    evm.topic_count = static_cast<uint8_t>(tc);

    for (std::size_t i = 0; i < tc; ++i) {
        utils::parse_hex_bytes(raw.topics[i], evm.topics[i]);
    }

    // Data (truncate)
    utils::validate_hex(raw.data);
    const std::size_t byte_len = (raw.data.size() - 2) / 2;

    if (byte_len > evm.data.size()) {
        evm.data_size = static_cast<uint32_t>(evm.data.size());
        evm.truncated = true;
    } else {
        evm.data_size = static_cast<uint32_t>(byte_len);
        evm.truncated = false;
    }

    utils::parse_hex_bytes(raw.data, evm.data);
    
    out.payload = evm;
}

} // namespace sentinel::events
