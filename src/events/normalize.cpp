#include "sentinel/events/normalize.hpp"
#include "sentinel/events/utils/hex.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sentinel::events {

namespace {

constexpr auto TOPIC_TRANSFER = utils::parse_topic_literal(
    "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef");
constexpr auto TOPIC_SWAP_V2 = utils::parse_topic_literal(
    "0xd78ad95fa46c994b6551d0da85fc275fe613ce37657fb8d5e3d130840159d822");
constexpr auto TOPIC_SWAP_V3 = utils::parse_topic_literal(
    "0xc42079f94a6350d7e5735f2a153e368fc95153e172ee90824036511a8fb417b3");
constexpr auto TOPIC_MINT = utils::parse_topic_literal(
    "0x4c209b5fc8ad50758f13e2e1088ba56a560dff694af163234d7f6c31034c568d");
constexpr auto TOPIC_BURN = utils::parse_topic_literal(
    "0xdccd28e36055d506992d9d40a08e08d6691c9569707248066f2c2f82998396e9");

// Governance/Admin Topics
constexpr auto TOPIC_OWNERSHIP_TRANSFERRED = utils::parse_topic_literal(
    "0x8be0079c531659141344cd1fd0a4f28419497f9722a3daafe3b4186f6b6457e0");
constexpr auto TOPIC_PAUSED = utils::parse_topic_literal(
    "0x62e78cea01bee320cd4e420270b5ea74000d11b0c9f74754ebdbfc544b05a258");
constexpr auto TOPIC_UNPAUSED = utils::parse_topic_literal(
    "0x5db9ee0a495bf2e6ff9c91a7834c1ba4fdd244a5e8aa4e537bd38aeae4b073aa");
constexpr auto TOPIC_ROLE_GRANTED = utils::parse_topic_literal(
    "0x2f8788117e7eff1d82e926ec794901d17c78024a50270940304540a733656f0d");
constexpr auto TOPIC_ROLE_REVOKED = utils::parse_topic_literal(
    "0xf6391f5c32d9c69d2a47ea670b442974b53935d1edc7fd64eb21e047a839171b");
constexpr auto TOPIC_UPGRADED = utils::parse_topic_literal(
    "0xbc7cd75a20ee27fd9adebab32041f755214dbc6bffa90cc0225b39da2e5c2d3b");

sentinel::risk::SignalType
classify_topic0(const std::array<uint8_t, 32> &topic0) {
  if (topic0 == TOPIC_TRANSFER)
    return sentinel::risk::SignalType::Transfer;
  if (topic0 == TOPIC_SWAP_V2)
    return sentinel::risk::SignalType::Swap;
  if (topic0 == TOPIC_SWAP_V3)
    return sentinel::risk::SignalType::Swap;
  if (topic0 == TOPIC_MINT)
    return sentinel::risk::SignalType::LiquidityChange;
  if (topic0 == TOPIC_BURN)
    return sentinel::risk::SignalType::LiquidityChange;
  
  if (topic0 == TOPIC_OWNERSHIP_TRANSFERRED ||
      topic0 == TOPIC_PAUSED ||
      topic0 == TOPIC_UNPAUSED ||
      topic0 == TOPIC_ROLE_GRANTED ||
      topic0 == TOPIC_ROLE_REVOKED ||
      topic0 == TOPIC_UPGRADED) {
    return sentinel::risk::SignalType::Governance;
  }

  return sentinel::risk::SignalType::Unknown;
}

} // namespace

void normalize(const RawLog &raw, sentinel::risk::Signal &out,
               uint64_t chain_id, uint64_t block_timestamp) {
  out = sentinel::risk::Signal{}; // safe zero-init

  // Signal classification will be done after payload extraction

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

  evm.tx_index = static_cast<uint32_t>(txi);
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

  if (evm.topic_count > 0) {
    out.type = classify_topic0(evm.topics[0]);

    if (out.type == sentinel::risk::SignalType::Governance) {
      sentinel::risk::GovernanceAction action = sentinel::risk::GovernanceAction::Unknown;
      if (evm.topics[0] == TOPIC_OWNERSHIP_TRANSFERRED) {
        action = sentinel::risk::GovernanceAction::OwnershipTransferred;
      } else if (evm.topics[0] == TOPIC_PAUSED) {
        action = sentinel::risk::GovernanceAction::Paused;
      } else if (evm.topics[0] == TOPIC_UNPAUSED) {
        action = sentinel::risk::GovernanceAction::Unpaused;
      } else if (evm.topics[0] == TOPIC_ROLE_GRANTED) {
        action = sentinel::risk::GovernanceAction::RoleGranted;
      } else if (evm.topics[0] == TOPIC_ROLE_REVOKED) {
        action = sentinel::risk::GovernanceAction::RoleRevoked;
      } else if (evm.topics[0] == TOPIC_UPGRADED) {
        action = sentinel::risk::GovernanceAction::Upgraded;
      }
      
      sentinel::risk::GovernanceEvent gov{};
      gov.action = action;
      // Emit governance object to pipeline payload
      out.payload = gov;
    }

  } else {
    out.type = sentinel::risk::SignalType::Unknown;
  }
}

} // namespace sentinel::events
