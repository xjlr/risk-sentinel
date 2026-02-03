#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sentinel {

struct BlockMeta {
  std::string chain;      // "arbitrum"
  std::uint64_t number{}; // block number
  std::string hash;       // hex string
  std::uint64_t timestamp{}; // unix seconds
};

struct TxMeta {
  std::string hash;  // hex string
  std::string from;  // hex addr
  std::string to;    // hex addr or empty
  std::uint64_t nonce{};
};

struct LogEvent {
  std::string address;              // contract address
  std::vector<std::string> topics;  // hex topics
  std::string data;                 // hex data
  std::uint32_t log_index{};
};

struct NormalizedEvent {
  BlockMeta block;
  TxMeta tx;
  LogEvent log;
};

} // namespace sentinel
