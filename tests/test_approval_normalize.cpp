#include "sentinel/events/normalize.hpp"
#include "sentinel/events/utils/hex.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace sentinel::events;
using namespace sentinel::risk;

TEST_CASE("Approval Normalization Testing") {

  SECTION("Approval topic0 produces SignalType::Approval with EvmLogEvent payload") {
    RawLog raw{};
    raw.address = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcd";
    // ERC20 Approval topic0
    raw.topics.push_back("0x8c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b925");
    // owner (indexed)
    raw.topics.push_back("0x000000000000000000000000aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    // spender (indexed)
    raw.topics.push_back("0x000000000000000000000000bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    // amount = 1000 in 32 bytes
    raw.data = "0x00000000000000000000000000000000000000000000000000000000000003e8";
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x0";
    raw.logIndex = "0x0";
    raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 42161, 1234567890);

    REQUIRE(out.type == SignalType::Approval);
    const auto *evm = std::get_if<EvmLogEvent>(&out.payload);
    REQUIRE(evm != nullptr);
    REQUIRE(evm->chain_id == 42161);
    REQUIRE(evm->topic_count == 3);
    REQUIRE(evm->data_size == 32);
  }

  SECTION("Non-Approval topic0 does not produce SignalType::Approval") {
    RawLog raw{};
    raw.address = "0x1111111111111111111111111111111111111111";
    // Transfer topic0 instead of Approval
    raw.topics.push_back("0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef");
    raw.topics.push_back("0x000000000000000000000000aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    raw.topics.push_back("0x000000000000000000000000bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    raw.data = "0x00000000000000000000000000000000000000000000000000000000000003e8";
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x0";
    raw.logIndex = "0x0";
    raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 1234567890);

    REQUIRE(out.type != SignalType::Approval);
  }

  SECTION("Approval payload is EvmLogEvent, not a new struct") {
    RawLog raw{};
    raw.address = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcd";
    raw.topics.push_back("0x8c5be1e5ebec7d5bd14f71427d1e84f3dd0314c0f7b2291e5b200ac8c7c3b925");
    raw.topics.push_back("0x000000000000000000000000aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    raw.topics.push_back("0x000000000000000000000000bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");
    raw.data = "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
    raw.blockNumber = "0x1";
    raw.transactionIndex = "0x0";
    raw.logIndex = "0x0";
    raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 0);

    REQUIRE(out.type == SignalType::Approval);
    REQUIRE(std::get_if<EvmLogEvent>(&out.payload) != nullptr);
    // Governance/MintBurn/etc. structs must NOT be present
    REQUIRE(std::get_if<GovernanceEvent>(&out.payload) == nullptr);
    REQUIRE(std::get_if<MintBurnEvent>(&out.payload) == nullptr);
  }
}
