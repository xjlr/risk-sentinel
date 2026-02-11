#include "sentinel/chains/arbitrum/ArbitrumAdapter.hpp"
#include "sentinel/log.hpp"
#include "sentinel/events/utils/hex.hpp"

#include <stdexcept>
#include <string>

namespace {

// "0x..." hex string -> uint64
uint64_t parseHexU64(const std::string& hex, spdlog::logger& log) {
    if (hex.size() < 3 || hex.rfind("0x", 0) != 0) {
        log.error("invalid hex string (expected 0x...): '{}'", hex);
        throw std::runtime_error("Expected 0x-prefixed hex string, got: " + hex);
    }

    try {
        return std::stoull(hex.substr(2), nullptr, 16);
    } catch (const std::exception& e) {
        log.error("failed to parse hex '{}' : {}", hex, e.what());
        throw;
    }
}

} // namespace

ArbitrumAdapter::ArbitrumAdapter(JsonRpcClient& rpc)
    : rpc_(rpc)
    , log_(sentinel::logger(sentinel::LogComponent::Adapter)) {

    log_.info("ArbitrumAdapter initialized");
}

std::string ArbitrumAdapter::name() const {
    return "arbitrum";
}

uint64_t ArbitrumAdapter::chainId() {
    log_.debug("RPC call: eth_chainId");

    auto res = rpc_.call("eth_chainId");

    if (!res.contains("result")) {
        log_.error("eth_chainId response missing 'result': {}", res.dump());
        throw std::runtime_error("eth_chainId: missing result field");
    }

    const uint64_t cid = parseHexU64(res.at("result").get<std::string>(), log_);
    log_.debug("eth_chainId -> {}", cid);

    return cid;
}

uint64_t ArbitrumAdapter::latestBlock() {
    log_.debug("RPC call: eth_blockNumber");

    auto res = rpc_.call("eth_blockNumber");

    if (!res.contains("result")) {
        log_.error("eth_blockNumber response missing 'result': {}", res.dump());
        throw std::runtime_error("eth_blockNumber: missing result field");
    }

    const uint64_t block = parseHexU64(res.at("result").get<std::string>(), log_);
    log_.debug("eth_blockNumber -> {}", block);

    return block;
}

std::vector<sentinel::events::RawLog>
ArbitrumAdapter::getLogs(uint64_t from_block, uint64_t to_block) {
    using nlohmann::json;
    using sentinel::events::RawLog;

    if (to_block < from_block) {
        throw std::runtime_error("getLogs: to_block < from_block");
    }

    json filter{
        {"fromBlock", sentinel::events::utils::to_hex_quantity(from_block)},
        {"toBlock",   sentinel::events::utils::to_hex_quantity(to_block)}
        // later:
        // {"address", json::array({ "0x...", "0x..." })}
        // {"topics",  json::array({ "0x<topic0>", nullptr, nullptr, nullptr })}
    };

    log_.debug("RPC call: eth_getLogs filter={}", filter.dump());

    // JSON-RPC params: [ filter ]
    json params = json::array({filter});

    // JsonRpcClient::call() returns the whole response
    json res = rpc_.call("eth_getLogs", params);

    if (!res.contains("result") || !res["result"].is_array()) {
        log_.error("eth_getLogs: invalid response: {}", res.dump());
        throw std::runtime_error("eth_getLogs: missing result array");
    }

    const auto& arr = res["result"];
    log_.debug("eth_getLogs -> {} logs", arr.size());

    // MVP debug; TODO : Remove later
    if (arr.size() > 0 && arr.size() <= 5) {
        log_.debug("eth_getLogs sample payload={}", arr.dump());
    }

    if (!arr.empty()) {
        // A .dump(4) 4 spaces indentation
        log_.debug("Sample log[0] structure:\n{}", arr[0].dump(4));
    }

    std::vector<RawLog> out;
    out.reserve(arr.size());

    for (const auto& jlog : arr) {
        try {
            out.push_back(jlog.get<RawLog>());
        } catch (const std::exception& e) {
            log_.error("RawLog parse failed: {} json={}", e.what(), jlog.dump());
            throw;
        }
    }

    return out;
}

