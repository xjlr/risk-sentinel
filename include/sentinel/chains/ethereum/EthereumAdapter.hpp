#pragma once
// NOTE: This adapter is intentionally a near-duplicate of ArbitrumAdapter.
// Both speak vanilla EVM JSON-RPC (eth_chainId, eth_blockNumber, eth_getLogs,
// eth_getBlockByNumber); only name() differs. When a third EVM chain is
// added, both will be collapsed into a single EvmAdapter parameterised by
// chain name.
#include "sentinel/chains/ChainAdapter.hpp"
#include "sentinel/log.hpp"
#include "sentinel/rpc/JsonRpcClient.hpp"

class EthereumAdapter : public ChainAdapter {
public:
  explicit EthereumAdapter(JsonRpcClient &rpc);

  std::string name() const override;

  uint64_t chainId() override;
  uint64_t latestBlock() override;
  uint64_t blockTimestamp(uint64_t block_number) override;

  std::vector<sentinel::events::RawLog> getLogs(uint64_t from_block,
                                                uint64_t to_block) override;

private:
  JsonRpcClient &rpc_;
  spdlog::logger &log_;
};
