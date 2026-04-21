#include "sentinel/events/normalize.hpp"
#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/rules/large_transfer_rule.hpp"
#include "sentinel/risk/alert_formatter.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace sentinel::events;
using namespace sentinel::risk;

TEST_CASE("Regression: Existing Full Flow is preserved") {
  SECTION("Large Transfer Normalization -> Rule -> Formatter flow") {
    // 1. Normalize
    RawLog raw{};
    raw.address = "0x1111111111111111111111111111111111111111"; // Token address
    // ERC20 Transfer topic
    raw.topics.push_back("0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"); 
    // from = 0x2222...
    raw.topics.push_back("0x0000000000000000000000002222222222222222222222222222222222222222");
    // to = 0x3333...
    raw.topics.push_back("0x0000000000000000000000003333333333333333333333333333333333333333");

    // data = 2000 in hex (32 bytes)
    raw.data = "0x00000000000000000000000000000000000000000000000000000000000007d0";
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x1";
    raw.logIndex = "0x1";
    raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 1234567890);

    // It MUST NOT be hijacked by MintBurn
    REQUIRE(out.type == SignalType::Transfer);

    // 2. Rule Evaluation
    std::vector<LargeTransferRuleConfig> configs;
    std::array<uint8_t, 20> token_addr{};
    utils::parse_hex_bytes("0x1111111111111111111111111111111111111111", token_addr);

    configs.push_back({
        .customer_id = 99,
        .chain_id = 1,
        .token_address = token_addr,
        .threshold_be = utils::decimal_to_be_256("1000")
    });

    LargeTransferRule rule(configs);
    StateStore store;
    std::vector<Alert> alerts;

    rule.evaluate(out, store, alerts);
    REQUIRE(alerts.size() == 1);
    REQUIRE(alerts[0].customer_id == 99);
    REQUIRE(alerts[0].rule_type == "large_transfer");
    REQUIRE(alerts[0].amount_decimal == "2000");

    // 3. Formatter Output
    AlertFormatter formatter;
    std::unordered_map<std::uint64_t, std::string> customer_map = {{99, "Legacy Corp"}};
    std::unordered_map<TokenKey, std::string> token_map = {{{1, "0x1111111111111111111111111111111111111111"}, "USDC"}};
    
    std::string text = formatter.format_telegram(alerts[0], &customer_map, &token_map);
    REQUIRE(text.find("Legacy Corp") != std::string::npos);
    REQUIRE(text.find("Amount: 2000") != std::string::npos);
    REQUIRE(text.find("USDC") != std::string::npos);
    REQUIRE(text.find("MintBurn") == std::string::npos); // Should not have MintBurn strings
  }
}
