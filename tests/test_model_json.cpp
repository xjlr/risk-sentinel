#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "sentinel/json.hpp"
#include "sentinel/model.hpp"

TEST_CASE("Model JSON semantic") {
  sentinel::BlockMeta block = {"arbitrum", 123, "0xabc", 1700000000};
  sentinel::TxMeta tx = {"0xtx", "0xfrom", "0xto", 7};
  sentinel::LogEvent log = {"0xaddr", {"0xt0", "0xt1"}, "0xdata", 9};

  const nlohmann::json block_got = sentinel::to_json(block);
  const nlohmann::json tx_got = sentinel::to_json(tx);
  const nlohmann::json log_got = sentinel::to_json(log);

  const nlohmann::json block_expected = {
      {"chain", "arbitrum"},
      {"number", 123},
      {"hash", "0xabc"},
      {"timestamp", 1700000000}
  };
  
  const nlohmann::json tx_expected = {
      {"hash", "0xtx"},
      {"from", "0xfrom"},
      {"to", "0xto"},
      {"nonce", 7}
  };
  
  const nlohmann::json log_expected = {
      {"address", "0xaddr"},
      {"topics", {"0xt0", "0xt1"}},
      {"data", "0xdata"},
      {"log_index", 9}
  };

  REQUIRE(block_got == block_expected);
  REQUIRE(tx_got == tx_expected);
  REQUIRE(log_got == log_expected);
}
