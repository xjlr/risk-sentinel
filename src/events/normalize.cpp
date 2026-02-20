#include "sentinel/events/normalize.hpp"
#include "sentinel/events/utils/hex.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace sentinel::events {

void normalize(const RawLog& raw,
               NormalizedEvent& out,
               uint64_t chain_id,
               uint64_t block_timestamp)
{
    out = NormalizedEvent{}; // safe zero-init
    // Chain context
    out.chain_id = chain_id;
    out.block_timestamp = block_timestamp;

    // Meta (hex -> integers)
    out.block_number = utils::parse_hex_uint64(raw.blockNumber);

    const uint64_t txi = utils::parse_hex_uint64(raw.transactionIndex);
    const uint64_t lgi = utils::parse_hex_uint64(raw.logIndex);

    if (txi > std::numeric_limits<uint32_t>::max() ||
        lgi > std::numeric_limits<uint32_t>::max()) {
        throw std::runtime_error("tx_index/log_index out of range");
    }

    out.tx_index  = static_cast<uint32_t>(txi);
    out.log_index = static_cast<uint32_t>(lgi);

    out.removed = raw.removed;

    utils::parse_hex_bytes(raw.transactionHash, out.tx_hash);

    // Address
    utils::parse_hex_bytes(raw.address, out.contract);

    // Topics (max 4)
    const std::size_t tc = std::min<std::size_t>(raw.topics.size(), 4);
    out.topic_count = static_cast<uint8_t>(tc);

    for (std::size_t i = 0; i < tc; ++i) {
        utils::parse_hex_bytes(raw.topics[i], out.topics[i]);
    }

    // Data (truncate)
    utils::validate_hex(raw.data);
    const std::size_t byte_len = (raw.data.size() - 2) / 2;

    if (byte_len > out.data.size()) {
        out.data_size = static_cast<uint32_t>(out.data.size());
        out.truncated = true;
    } else {
        out.data_size = static_cast<uint32_t>(byte_len);
        out.truncated = false;
    }

    utils::parse_hex_bytes(raw.data, out.data);
}

} // namespace sentinel::events
