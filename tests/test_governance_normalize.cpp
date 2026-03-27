#include "sentinel/events/normalize.hpp"
#include "sentinel/events/utils/hex.hpp"
#include <catch2/catch_test_macros.hpp>

using namespace sentinel::events;
using namespace sentinel::risk;

TEST_CASE("Governance Normalization Testing") {
  SECTION("Topic0 to GovernanceAction mapping") {
    struct TestCase {
      std::string topic_hex;
      GovernanceAction expected_action;
    };

    std::vector<TestCase> cases = {
      {"0x8be0079c531659141344cd1fd0a4f28419497f9722a3daafe3b4186f6b6457e0", GovernanceAction::OwnershipTransferred},
      {"0x62e78cea01bee320cd4e420270b5ea74000d11b0c9f74754ebdbfc544b05a258", GovernanceAction::Paused},
      {"0x5db9ee0a495bf2e6ff9c91a7834c1ba4fdd244a5e8aa4e537bd38aeae4b073aa", GovernanceAction::Unpaused},
      {"0x2f8788117e7eff1d82e926ec794901d17c78024a50270940304540a733656f0d", GovernanceAction::RoleGranted},
      {"0xf6391f5c32d9c69d2a47ea670b442974b53935d1edc7fd64eb21e047a839171b", GovernanceAction::RoleRevoked},
      {"0xbc7cd75a20ee27fd9adebab32041f755214dbc6bffa90cc0225b39da2e5c2d3b", GovernanceAction::Upgraded}
    };

    for (const auto& tc : cases) {
      RawLog raw{};
      raw.address = "0x1111111111111111111111111111111111111111"; // Contract address
      raw.topics.push_back(tc.topic_hex);
      raw.data = "0x";
      raw.blockNumber = "0x100";
      raw.transactionIndex = "0x1";
      raw.logIndex = "0x1";
      raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

      Signal out;
      normalize(raw, out, 1, 1234567890);

      REQUIRE(out.type == SignalType::Governance);
      
      const auto* gov_payload = std::get_if<GovernanceEvent>(&out.payload);
      REQUIRE(gov_payload != nullptr);
      REQUIRE(gov_payload->action == tc.expected_action);
      REQUIRE(gov_payload->chain_id == 1);
      
      std::array<uint8_t, 20> expected_address{};
      utils::parse_hex_bytes("0x1111111111111111111111111111111111111111", expected_address);
      REQUIRE(gov_payload->contract_address == expected_address);
    }
  }

  SECTION("Non-Governance topics correctly fall through") {
    RawLog raw{};
    raw.address = "0x1111111111111111111111111111111111111111";
    // ERC20 Transfer topic (non-governance)
    raw.topics.push_back("0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef"); 
    raw.data = "0x";
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x1";
    raw.logIndex = "0x1";
    raw.transactionHash = "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 1234567890);

    REQUIRE(out.type != SignalType::Governance); // Should map to Transfer
    REQUIRE(out.type == SignalType::Transfer);
    const auto* gov_payload = std::get_if<GovernanceEvent>(&out.payload);
    REQUIRE(gov_payload == nullptr); // Should not contain governance specific struct
  }
}
