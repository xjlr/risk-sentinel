#pragma once

#include "signal.hpp"
#include <array>
#include <cstdint>
#include <functional>
#include <string>

namespace sentinel::risk {

struct MintBurnRuleConfig {
  uint64_t customer_id;
  uint64_t chain_id;
  std::string contract_address;
  std::array<uint8_t, 32> mint_threshold_be;
  std::array<uint8_t, 32> burn_threshold_be;
  bool enabled;
};

struct MintBurnContractKey {
  uint64_t chain_id;
  std::string contract_address;

  bool operator==(const MintBurnContractKey &other) const {
    return chain_id == other.chain_id &&
           contract_address == other.contract_address;
  }
};

} // namespace sentinel::risk

template <> struct std::hash<sentinel::risk::MintBurnContractKey> {
  std::size_t
  operator()(const sentinel::risk::MintBurnContractKey &key) const {
    return std::hash<uint64_t>()(key.chain_id) ^
           (std::hash<std::string>()(key.contract_address) << 1);
  }
};
