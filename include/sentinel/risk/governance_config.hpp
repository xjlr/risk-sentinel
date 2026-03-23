#pragma once

#include <cstdint>
#include <string>
#include <functional>

namespace sentinel::risk {

struct GovernanceRuleConfig {
  uint64_t customer_id;
  uint64_t chain_id;
  std::string contract_address;
  bool enabled;
};

struct GovernanceContractKey {
  uint64_t chain_id;
  std::string contract_address;

  bool operator==(const GovernanceContractKey &other) const {
    return chain_id == other.chain_id &&
           contract_address == other.contract_address;
  }
};

} // namespace sentinel::risk

template <> struct std::hash<sentinel::risk::GovernanceContractKey> {
  std::size_t operator()(const sentinel::risk::GovernanceContractKey &key) const {
    return std::hash<uint64_t>()(key.chain_id) ^
           (std::hash<std::string>()(key.contract_address) << 1);
  }
};
