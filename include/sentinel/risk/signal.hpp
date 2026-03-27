#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

#include <rigtorp/SPSCQueue.h>

namespace sentinel::risk {

// Alias RingBuffer to rigtorp::SPSCQueue as specified
template <typename T> using RingBuffer = rigtorp::SPSCQueue<T>;

enum class SignalType : uint8_t {
  Unknown,

  // On-chain events
  Swap,
  Transfer,
  LiquidityChange,
  PriceTick,
  PoolSnapshot,
  Governance, // Minimal placeholder for future governance non-transfer alerts

  // Internal signals
  Control
};

constexpr std::size_t SignalTypeCount = 8;

struct SignalMeta {
  uint64_t timestamp_ms;
  std::optional<uint64_t> block_number;
  std::optional<std::array<uint8_t, 32>> tx_hash;
  bool is_final;
  uint32_t source_id; // debug only, not used for routing
};

// Payload Types (No virtual methods, POD-like)
struct EvmLogEvent {
  uint64_t chain_id;
  uint32_t tx_index;
  uint32_t log_index;
  bool removed;
  std::array<uint8_t, 20> address;
  uint8_t topic_count;
  std::array<std::array<uint8_t, 32>, 4> topics;
  uint32_t data_size;
  std::array<uint8_t, 256> data;
  bool truncated;
};

struct PriceTick {
  uint64_t price; // Example
};

struct PoolSnapshot {
  uint64_t reserves[2]; // Example
};

struct ControlSignal {
  enum class Command { Stop, Sync } command;
};

enum class GovernanceAction : uint8_t {
  Unknown,
  OwnershipTransferred,
  Paused,
  Unpaused,
  RoleGranted,
  RoleRevoked,
  Upgraded
};

struct GovernanceEvent {
  GovernanceAction action;
  uint64_t chain_id;
  std::array<uint8_t, 20> contract_address;
};

// Use std::variant, no inheritance
using SignalPayload = std::variant<std::monostate, EvmLogEvent, PriceTick,
                                   PoolSnapshot, GovernanceEvent, ControlSignal>;

struct Signal {
  SignalType type;
  SignalMeta meta;
  SignalPayload payload;
};

// Bitmask for checking rule interests
using SignalMask = uint32_t;

inline constexpr SignalMask make_mask(SignalType type) {
  return 1 << static_cast<uint8_t>(type);
}

} // namespace sentinel::risk
