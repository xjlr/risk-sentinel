#pragma once
#include <nlohmann/json.hpp>
#include "sentinel/model.hpp"

namespace sentinel {

inline nlohmann::json to_json(const BlockMeta& b) {
  return {
    {"chain", b.chain},
    {"number", b.number},
    {"hash", b.hash},
    {"timestamp", b.timestamp}
  };
}

inline nlohmann::json to_json(const TxMeta& t) {
  return {
    {"hash", t.hash},
    {"from", t.from},
    {"to", t.to},
    {"nonce", t.nonce}
  };
}

inline nlohmann::json to_json(const LogEvent& e) {
  return {
    {"address", e.address},
    {"topics", e.topics},
    {"data", e.data},
    {"log_index", e.log_index}
  };
}



} // namespace sentinel
