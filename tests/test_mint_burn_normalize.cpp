#include "sentinel/events/normalize.hpp"
#include "sentinel/events/utils/hex.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace sentinel::events;
using namespace sentinel::risk;

TEST_CASE("MintBurn Normalization Testing") {

  SECTION("Mint inference from zero-address 'from' topic") {
    RawLog raw{};
    raw.address = "0x1111111111111111111111111111111111111111"; // Contract address
    // ERC20 Transfer topic
    raw.topics.push_back("0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"); 
    // from = 0x0 (32 bytes zero-padded)
    raw.topics.push_back("0x0000000000000000000000000000000000000000000000000000000000000000");
    // to = 0x222222...
    raw.topics.push_back("0x0000000000000000000000002222222222222222222222222222222222222222");

    // data = 1000 in hex, 32 bytes
    raw.data = "0x00000000000000000000000000000000000000000000000000000000000003e8";
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x1";
    raw.logIndex = "0x1";
    raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 1234567890);

    REQUIRE(out.type == SignalType::MintBurn); // Mint/Burn priority over Generic Transfer
    const auto* mb_payload = std::get_if<MintBurnEvent>(&out.payload);
    REQUIRE(mb_payload != nullptr);
    REQUIRE(mb_payload->direction == MintBurnDirection::Mint);
  }

  SECTION("Burn inference from zero-address 'to' topic") {
    RawLog raw{};
    raw.address = "0x1111111111111111111111111111111111111111";
    raw.topics.push_back("0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"); 
    // from = 0x2222... (not zero)
    raw.topics.push_back("0x0000000000000000000000002222222222222222222222222222222222222222");
    // to = 0x0
    raw.topics.push_back("0x0000000000000000000000000000000000000000000000000000000000000000");

    raw.data = "0x00000000000000000000000000000000000000000000000000000000000003e8";
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x1";
    raw.logIndex = "0x1";
    raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 1234567890);

    REQUIRE(out.type == SignalType::MintBurn); 
    const auto* mb_payload = std::get_if<MintBurnEvent>(&out.payload);
    REQUIRE(mb_payload != nullptr);
    REQUIRE(mb_payload->direction == MintBurnDirection::Burn);
  }

  SECTION("Standard Transfer is NOT treated as MintBurn") {
    RawLog raw{};
    raw.address = "0x1111111111111111111111111111111111111111"; // Contract address
    raw.topics.push_back("0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"); 
    raw.topics.push_back("0x000000000000000000000000aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    raw.topics.push_back("0x000000000000000000000000bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    raw.data = "0x00000000000000000000000000000000000000000000000000000000000003e8";
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x1";
    raw.logIndex = "0x1";
    raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 1234567890);

    REQUIRE(out.type == SignalType::Transfer); // should be untouched generic transfer
  }

  SECTION("Explicit Edge Case: zero-address detection is based on full 32 bytes") {
    RawLog raw{};
    raw.address = "0x1111111111111111111111111111111111111111";
    raw.topics.push_back("0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"); 
    raw.topics.push_back("0x0000000000000000000000002222222222222222222222222222222222222222"); // valid 'from' non-zero
    // 'to' has last 20 bytes zero, first 12 bytes not zero (invalidly padded zero-address)
    raw.topics.push_back("0xffffffffffffffffffffffff0000000000000000000000000000000000000000");

    raw.data = "0x00000000000000000000000000000000000000000000000000000000000003e8";
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x1";
    raw.logIndex = "0x1";
    raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 1234567890);

    // Evaluates purely to transfer since full 32-bytes wasn't 0
    REQUIRE(out.type == SignalType::Transfer); 
    const auto* payload = std::get_if<EvmLogEvent>(&out.payload);
    REQUIRE(payload != nullptr);
  }
}
