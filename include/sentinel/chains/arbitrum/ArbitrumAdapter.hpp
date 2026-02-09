#pragma once
#include "sentinel/chains/ChainAdapter.hpp"
#include "sentinel/rpc/JsonRpcClient.hpp"
#include "sentinel/log.hpp"

class ArbitrumAdapter : public ChainAdapter {
public:
    explicit ArbitrumAdapter(JsonRpcClient& rpc);

    std::string name() const override;

    uint64_t chainId() override;
    uint64_t latestBlock() override;

    std::vector<sentinel::events::RawLog>
    getLogs(uint64_t from_block, uint64_t to_block) override;

private:
    JsonRpcClient& rpc_;
    spdlog::logger& log_;
};

