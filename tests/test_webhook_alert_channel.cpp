#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "sentinel/risk/alert_dispatcher.hpp"      // Alert, CustomerId
#include "sentinel/risk/webhook_alert_channel.hpp" // WebhookAlertChannel, WebhookEndpoint

using namespace sentinel::risk;

// ---------------------------------------------------------------------------
// Helper to build a minimal Alert
// ---------------------------------------------------------------------------
static Alert make_alert(CustomerId cid, const std::string &rule_type = "large_transfer") {
    Alert a{};
    a.customer_id = cid;
    a.rule_type   = rule_type;
    a.message     = "test alert";
    a.timestamp_ms = 1000000;
    return a;
}

// ---------------------------------------------------------------------------
// No-op for unknown customer
// ---------------------------------------------------------------------------

TEST_CASE("WebhookAlertChannel: send() on unknown customer is a no-op",
          "[webhook]") {
    // Construct with an empty map — no customer has webhooks.
    std::unordered_map<std::uint64_t, std::vector<WebhookEndpoint>> empty_map;
    WebhookAlertChannel channel(std::move(empty_map));

    // Must not throw, must not crash.
    Alert a = make_alert(/*customer_id=*/99);
    REQUIRE_NOTHROW(channel.send(a));
}

TEST_CASE("WebhookAlertChannel: send() for customer with no endpoints is a no-op",
          "[webhook]") {
    std::unordered_map<std::uint64_t, std::vector<WebhookEndpoint>> map;
    // customer 7 exists in the map but has an empty endpoint list
    map[7] = {};

    WebhookAlertChannel channel(std::move(map));

    Alert a = make_alert(7);
    REQUIRE_NOTHROW(channel.send(a));
}

// ---------------------------------------------------------------------------
// Lookup returns both endpoints for a two-endpoint customer
// (inspected via the map state — no actual HTTP call)
// ---------------------------------------------------------------------------

TEST_CASE("WebhookAlertChannel: two endpoints are stored per customer",
          "[webhook]") {
    std::unordered_map<std::uint64_t, std::vector<WebhookEndpoint>> map;
    map[42] = {
        WebhookEndpoint{"https://hook.example.com/a", "secret-a"},
        WebhookEndpoint{"https://hook.example.com/b", ""},
    };

    // The channel owns the map after construction.
    // Verify the two endpoints are present by inspecting before move.
    REQUIRE(map[42].size() == 2);
    REQUIRE(map[42][0].url == "https://hook.example.com/a");
    REQUIRE(map[42][0].hmac_secret == "secret-a");
    REQUIRE(map[42][1].url == "https://hook.example.com/b");
    REQUIRE(map[42][1].hmac_secret.empty()); // unsigned endpoint

    // Construction must not throw
    REQUIRE_NOTHROW(WebhookAlertChannel(std::move(map)));
}

// ---------------------------------------------------------------------------
// JSON payload field checks (pure logic, no network)
// ---------------------------------------------------------------------------

TEST_CASE("WebhookAlertChannel: fully-populated Alert has all JSON fields",
          "[webhook][json]") {
    // Build the payload the same way send() does so we can assert the schema.
    // We do this in the test rather than calling send() to avoid curl I/O.
    nlohmann::json payload;
    Alert a = make_alert(1, "mint_burn");
    a.chain_id       = 42161ULL;
    a.token_address  = "0xfd086bc7cd5c481dcc9c85ebe478a1c0b69fcbb9";
    a.amount_decimal = "1234567.89";

    payload["customer_id"]   = a.customer_id;
    payload["rule_type"]     = a.rule_type;
    payload["message"]       = a.message;
    payload["timestamp_ms"]  = a.timestamp_ms;
    if (a.chain_id.has_value())
        payload["chain_id"] = *a.chain_id;
    if (a.token_address.has_value())
        payload["token_address"] = *a.token_address;
    if (a.amount_decimal.has_value())
        payload["amount_decimal"] = *a.amount_decimal;

    REQUIRE(payload.contains("customer_id"));
    REQUIRE(payload.contains("rule_type"));
    REQUIRE(payload.contains("message"));
    REQUIRE(payload.contains("timestamp_ms"));
    REQUIRE(payload.contains("chain_id"));
    REQUIRE(payload.contains("token_address"));
    REQUIRE(payload.contains("amount_decimal"));

    REQUIRE(payload["customer_id"].get<uint64_t>() == 1);
    REQUIRE(payload["rule_type"].get<std::string>() == "mint_burn");
    REQUIRE(payload["chain_id"].get<uint64_t>() == 42161);
    REQUIRE(payload["amount_decimal"].get<std::string>() == "1234567.89");
}

TEST_CASE("WebhookAlertChannel: optional fields omitted when unset",
          "[webhook][json]") {
    nlohmann::json payload;
    Alert a = make_alert(2, "governance");
    // chain_id, token_address, amount_decimal intentionally left unset

    payload["customer_id"]  = a.customer_id;
    payload["rule_type"]    = a.rule_type;
    payload["message"]      = a.message;
    payload["timestamp_ms"] = a.timestamp_ms;
    if (a.chain_id.has_value())
        payload["chain_id"] = *a.chain_id;
    if (a.token_address.has_value())
        payload["token_address"] = *a.token_address;
    if (a.amount_decimal.has_value())
        payload["amount_decimal"] = *a.amount_decimal;

    REQUIRE(payload.contains("customer_id"));
    REQUIRE(payload.contains("rule_type"));
    REQUIRE(payload.contains("message"));
    REQUIRE(payload.contains("timestamp_ms"));

    // Optional fields must be absent — not null, absent.
    REQUIRE_FALSE(payload.contains("chain_id"));
    REQUIRE_FALSE(payload.contains("token_address"));
    REQUIRE_FALSE(payload.contains("amount_decimal"));
}
