#include <catch2/catch_test_macros.hpp>

#include "sentinel/risk/alert_deduplicator.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"

using namespace sentinel::risk;

static Alert make_alert(uint64_t customer_id,
                        const std::string& rule_type,
                        std::optional<uint64_t> chain_id = std::nullopt,
                        std::optional<std::string> token = std::nullopt) {
    Alert a;
    a.customer_id = customer_id;
    a.rule_type = rule_type;
    a.message = "test";
    a.timestamp_ms = 0;
    a.chain_id = chain_id;
    a.token_address = token;
    return a;
}

static DeduplicatorConfig make_config() {
    DeduplicatorConfig cfg;
    cfg.default_window_ms = 60'000;
    cfg.per_rule_window_ms = {
        {"governance",     3'600'000},
        {"large_transfer", 60'000},
        {"approval",       300'000},
    };
    cfg.cleanup_every_n_alerts = 10'000; // disable auto-cleanup in most tests
    return cfg;
}

TEST_CASE("AlertDeduplicator — first alert is not suppressed") {
    AlertDeduplicator dedup(make_config());
    auto a = make_alert(1, "large_transfer", 42161, "0xtoken");
    CHECK_FALSE(dedup.should_suppress(a, 1'000));
}

TEST_CASE("AlertDeduplicator — second identical alert within window is suppressed") {
    AlertDeduplicator dedup(make_config());
    auto a = make_alert(1, "large_transfer", 42161, "0xtoken");
    CHECK_FALSE(dedup.should_suppress(a, 1'000));
    CHECK(dedup.should_suppress(a, 1'000 + 30'000)); // 30 s later, window is 60 s
}

TEST_CASE("AlertDeduplicator — second identical alert after window is not suppressed and updates timestamp") {
    AlertDeduplicator dedup(make_config());
    auto a = make_alert(1, "large_transfer", 42161, "0xtoken");
    CHECK_FALSE(dedup.should_suppress(a, 1'000));
    CHECK_FALSE(dedup.should_suppress(a, 1'000 + 61'000)); // 61 s later, past 60 s window

    // A third call immediately after the second should now be suppressed
    CHECK(dedup.should_suppress(a, 1'000 + 61'000 + 1));
}

TEST_CASE("AlertDeduplicator — different customer_id — neither suppresses the other") {
    AlertDeduplicator dedup(make_config());
    auto a1 = make_alert(1, "large_transfer", 42161, "0xtoken");
    auto a2 = make_alert(2, "large_transfer", 42161, "0xtoken");
    CHECK_FALSE(dedup.should_suppress(a1, 1'000));
    CHECK_FALSE(dedup.should_suppress(a2, 1'000));
}

TEST_CASE("AlertDeduplicator — different rule_type — neither suppresses the other") {
    AlertDeduplicator dedup(make_config());
    auto a1 = make_alert(1, "large_transfer", 42161, "0xtoken");
    auto a2 = make_alert(1, "governance", 42161, "0xtoken");
    CHECK_FALSE(dedup.should_suppress(a1, 1'000));
    CHECK_FALSE(dedup.should_suppress(a2, 1'000));
}

TEST_CASE("AlertDeduplicator — different token_address — neither suppresses the other") {
    AlertDeduplicator dedup(make_config());
    auto a1 = make_alert(1, "large_transfer", 42161, "0xtokenA");
    auto a2 = make_alert(1, "large_transfer", 42161, "0xtokenB");
    CHECK_FALSE(dedup.should_suppress(a1, 1'000));
    CHECK_FALSE(dedup.should_suppress(a2, 1'000));
}

TEST_CASE("AlertDeduplicator — nullopt chain_id and token_address handled correctly") {
    AlertDeduplicator dedup(make_config());
    auto a = make_alert(1, "governance"); // chain_id=nullopt, token=nullopt
    CHECK_FALSE(dedup.should_suppress(a, 1'000));
    CHECK(dedup.should_suppress(a, 1'000 + 500)); // within 1-hour governance window
}

TEST_CASE("AlertDeduplicator — per-rule window override") {
    AlertDeduplicator dedup(make_config());
    // governance window = 1 h; large_transfer window = 1 min (default)

    auto gov = make_alert(1, "governance",     42161, "0xcontract");
    auto lt  = make_alert(1, "large_transfer", 42161, "0xtoken");

    // Both fire at t=0
    CHECK_FALSE(dedup.should_suppress(gov, 0));
    CHECK_FALSE(dedup.should_suppress(lt,  0));

    // At 30 minutes: governance (1 h window) is suppressed; large_transfer (1 min window) is not
    const uint64_t t30m = 30ULL * 60'000;
    CHECK(dedup.should_suppress(gov, t30m));
    CHECK_FALSE(dedup.should_suppress(lt, t30m));
}

TEST_CASE("AlertDeduplicator — cleanup_stale_entries removes old entries and keeps fresh ones") {
    AlertDeduplicator dedup(make_config());
    // max_configured_window_ms_ = max(60000, 3600000, 60000, 300000) = 3600000

    auto a1 = make_alert(1, "large_transfer", 42161, "0xtoken");
    auto a2 = make_alert(2, "large_transfer", 42161, "0xtoken");

    // Both fire at t=0
    dedup.should_suppress(a1, 0);
    dedup.should_suppress(a2, 0);
    REQUIRE(dedup.tracked_keys_count() == 2);

    // a2 fires again at t=3'600'001 (past 1 hour and 1ms window, not suppressed → update timestamp)
    dedup.should_suppress(a2, 3'600'001);

    // Cleanup at t=7'200'001:
    //   a1: 7200001 - 0 = 7200001 > 3600000 → stale
    //   a2: 7200001 - 3600001 = 3600000, NOT > 3600000 → kept
    size_t removed = dedup.cleanup_stale_entries(7'200'001);
    CHECK(removed == 1);
    CHECK(dedup.tracked_keys_count() == 1);
}

TEST_CASE("AlertDeduplicator — clock skew: now_ms < stored_timestamp suppresses and does not underflow") {
    AlertDeduplicator dedup(make_config());
    auto a = make_alert(1, "large_transfer", 42161, "0xtoken");

    // Record at t=1000
    CHECK_FALSE(dedup.should_suppress(a, 1'000));

    // Simulate clock going backwards to t=500 — must suppress, not wrap-around
    CHECK(dedup.should_suppress(a, 500));

    // cleanup_stale_entries with a past now_ms must not remove the entry
    size_t removed = dedup.cleanup_stale_entries(500);
    CHECK(removed == 0);
    CHECK(dedup.tracked_keys_count() == 1);
}

TEST_CASE("AlertDeduplicator — tracked_keys_count decreases after cleanup") {
    AlertDeduplicator dedup(make_config());
    auto a = make_alert(1, "large_transfer", 42161, "0xtoken");
    dedup.should_suppress(a, 0);
    REQUIRE(dedup.tracked_keys_count() == 1);

    // At t=3'600'001: 3600001 - 0 = 3600001 > 3600000 → stale
    dedup.cleanup_stale_entries(3'600'001);
    CHECK(dedup.tracked_keys_count() == 0);
}
