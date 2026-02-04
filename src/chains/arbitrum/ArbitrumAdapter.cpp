#include "sentinel/chains/arbitrum/ArbitrumAdapter.hpp"

#include <stdexcept>
#include <string>

namespace {

// "0x..." hex string -> uint64
uint64_t parseHexU64(const std::string& hex) {
    if (hex.size() < 3 || hex.rfind("0x", 0) != 0) {
        throw std::runtime_error("Expected 0x-prefixed hex string, got: " + hex);
    }
    // stoull with base=16; handles up to uint64
    return std::stoull(hex.substr(2), nullptr, 16);
}

} // namespace

ArbitrumAdapter::ArbitrumAdapter(JsonRpcClient& rpc)
    : rpc_(rpc) {}

uint64_t ArbitrumAdapter::chainId() {
    auto res = rpc_.call("eth_chainId");
    return parseHexU64(res.at("result").get<std::string>());
}

uint64_t ArbitrumAdapter::latestBlock() {
    auto res = rpc_.call("eth_blockNumber");
    return parseHexU64(res.at("result").get<std::string>());
}
