#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/alert_formatter.hpp"
#include "sentinel/risk/signal.hpp"
#include <catch2/catch_test_macros.hpp>
#include <unordered_map>

using namespace sentinel::risk;
using namespace sentinel::events::utils;

TEST_CASE("Alert Formatter Testing") {
  AlertFormatter formatter;

  std::unordered_map<std::uint64_t, std::string> customer_map = {
      {1, "Customer One"},
      {2, "Customer Two"}
  };

  std::unordered_map<TokenKey, std::string> token_map = {
      {{1, "0x1111111111111111111111111111111111111111"}, "USDC"},
      {{1, "0x2222222222222222222222222222222222222222"}, "DAI"}
  };

  SECTION("Governance formatting omits Amount and Token in Telegram") {
    Alert a;
    a.customer_id = 1;
    a.rule_type = "governance";
    a.timestamp_ms = 10000;
    a.chain_id = 1;
    a.token_address = "0x1111111111111111111111111111111111111111"; // Contract logic uses token_address for the address
    a.message = "Governance action 'Paused' detected on contract 0x111...111";
    // Usually no amount_decimal for governance, but let's test if it was accidentally populated
    a.amount_decimal = "1000.0";

    std::string text = formatter.format_telegram(a, &customer_map, &token_map);

    REQUIRE(text.find("Customer: Customer One") != std::string::npos);
    REQUIRE(text.find("Type: Governance") != std::string::npos);
    REQUIRE(text.find("Time: 10000") != std::string::npos);
    REQUIRE(text.find("Message: Governance action 'Paused'") != std::string::npos);
    
    // Ensure irrelevant fields are omitted
    REQUIRE(text.find("Amount:") == std::string::npos);
    REQUIRE(text.find("Token:") == std::string::npos);
    // Note that the token "USDC" mapping should not be pulled in
    REQUIRE(text.find("USDC") == std::string::npos);
  }

  SECTION("Governance formatting omits Amount and Token in Console") {
    Alert a;
    a.customer_id = 1;
    a.rule_type = "governance";
    a.timestamp_ms = 20000;
    a.chain_id = 1;
    a.token_address = "0x1111111111111111111111111111111111111111"; 
    a.message = "Governance action 'RoleGranted'";
    a.amount_decimal = "2000.0";

    std::string text = formatter.format_console(a);

    REQUIRE(text.find("Executing Webhook for: Governance action 'RoleGranted'") != std::string::npos);
    REQUIRE(text.find("[Time: 20000]") != std::string::npos);
    
    // Ensure irrelevant fields are omitted
    REQUIRE(text.find("[Amount:") == std::string::npos);
    REQUIRE(text.find("[Token:") == std::string::npos);
  }

  SECTION("Regression: Large Transfer formatting includes Amount and Token in Telegram") {
    Alert a;
    a.customer_id = 2;
    a.rule_type = "large_transfer";
    a.timestamp_ms = 30000;
    a.chain_id = 1;
    a.token_address = "0x1111111111111111111111111111111111111111";
    a.message = "Large Transfer detected";
    a.amount_decimal = "50000.00";

    std::string text = formatter.format_telegram(a, &customer_map, &token_map);

    REQUIRE(text.find("Customer: Customer Two") != std::string::npos);
    REQUIRE(text.find("Message: Large Transfer detected") != std::string::npos);
    REQUIRE(text.find("Time: 30000") != std::string::npos);
    
    // Ensure existing formatting is untouched
    REQUIRE(text.find("Type: Governance") == std::string::npos);
    REQUIRE(text.find("Amount: 50000.00") != std::string::npos);
    REQUIRE(text.find("Token: USDC") != std::string::npos); // Should map correctly
  }

  SECTION("Regression: Large Transfer formatting includes Amount and Token in Console") {
    Alert a;
    a.customer_id = 2;
    a.rule_type = "large_transfer";
    a.timestamp_ms = 40000;
    a.chain_id = 1;
    a.token_address = "0x2222222222222222222222222222222222222222";
    a.message = "Large Transfer detected";
    a.amount_decimal = "100.0";

    std::string text = formatter.format_console(a);

    REQUIRE(text.find("[Time: 40000]") != std::string::npos);
    REQUIRE(text.find("[Amount: 100.0]") != std::string::npos);
    REQUIRE(text.find("[Token: 0x2222222222222222222222222222222222222222]") != std::string::npos);
  }

  SECTION("Mint/Burn formatting includes Mint/Burn details in Telegram") {
    Alert a;
    a.customer_id = 1;
    a.rule_type = "mint_burn";
    a.timestamp_ms = 50000;
    a.chain_id = 1;
    a.token_address = "0x1111111111111111111111111111111111111111";
    a.message = "Large Mint detected";
    a.amount_decimal = "1234.56";

    std::string text = formatter.format_telegram(a, &customer_map, &token_map);

    REQUIRE(text.find("Customer: Customer One") != std::string::npos);
    REQUIRE(text.find("Type: Mint/Burn") != std::string::npos);
    REQUIRE(text.find("Message: Large Mint detected") != std::string::npos);
    REQUIRE(text.find("Chain ID: 1") != std::string::npos);
    REQUIRE(text.find("Amount: 1234.56") != std::string::npos);
    REQUIRE(text.find("Token: USDC (0x1111111111111111111111111111111111111111)") != std::string::npos);
  }
}
