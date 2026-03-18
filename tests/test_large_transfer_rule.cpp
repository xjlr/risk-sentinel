#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/rules/large_transfer_rule.hpp"
#include "sentinel/risk/signal.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace sentinel::risk;
using namespace sentinel::events::utils;

TEST_CASE("Large Transfer Rule Testing") {
  std::vector<LargeTransferRuleConfig> configs;
  std::array<uint8_t, 20> token_addr{};
  parse_hex_bytes("0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9", token_addr);

  configs.push_back({
      .customer_id = 1,
      .chain_id = 42161,
      .token_address = token_addr,
      .threshold_be = decimal_to_be_256("1000") // small threshold
  });

  LargeTransferRule rule(configs);

  SECTION("Rule skips irrelevant signals") {
    Signal s;
    s.type = SignalType::Unknown;

    StateStore store;
    std::vector<Alert> alerts;

    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.empty());
  }

  SECTION("Rule matches and emits correct customer_id alert") {
    Signal s;
    s.type = SignalType::Transfer;
    s.meta.timestamp_ms = 12345;

    EvmLogEvent evm;
    evm.chain_id = 42161;
    evm.address = token_addr;
    evm.removed = false;
    evm.topic_count = 3;
    evm.data_size = 32;
    evm.truncated = false;

    // 2000 > 1000
    auto data_be = decimal_to_be_256("2000");
    std::memcpy(evm.data.data(), data_be.data(), 32);

    s.payload = evm;

    StateStore store;
    std::vector<Alert> alerts;

    rule.evaluate(s, store, alerts);
    REQUIRE(alerts.size() == 1);
    REQUIRE(alerts[0].customer_id == 1);
    REQUIRE(alerts[0].rule_type == "large_transfer");
    REQUIRE(alerts[0].timestamp_ms == 12345);
  }
}
