#include "sentinel/events/normalize.hpp"
#include "sentinel/events/utils/hex.hpp"

#include <algorithm>
#include <stdexcept>
#include <string_view>

namespace sentinel::events {

namespace utils {

// Minimal helpers – ide jön a te parse_hex_uint64 / parse_hex_bytes.
// Feltételezem, hogy dobsz exceptiont hibánál.
#if 0
uint64_t parse_hex_uint64(std::string_view hex);

template <std::size_t N>
void parse_hex_bytes(std::string_view hex, std::array<uint8_t, N>& out);

// Opcionális: validálja a "0x" prefixet és páros hosszot.
inline void validate_hex(std::string_view hex) {
    if (hex.size() < 2 || hex.substr(0,2) != "0x") {
        throw std::runtime_error("Expected 0x-prefixed hex string");
    }
    if (((hex.size() - 2) % 2) != 0) {
        throw std::runtime_error("Hex string has odd length");
    }
}
#endif


} // namespace utils

void normalize(const RawLog& raw,
               NormalizedEvent& out,
               uint64_t chain_id,
               uint64_t block_timestamp)
{
    out = NormalizedEvent{}; // safe zero-init (ha nincs konstruktorod)

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

    // parse only up to out.data_size bytes (helperednek ezt tudnia kell)
    // Ha a parse_hex_bytes() mindig N byte-ot tölt, akkor itt inkább olyan kell,
    // ami limitálható: parse_hex_bytes_prefix(raw.data, out.data.data(), out.data_size);
    // MVP: töltsd a teljes out.data-t, de csak out.data_size-ot tekintsd érvényesnek:
    utils::parse_hex_bytes(raw.data, out.data);
}

} // namespace sentinel::events
