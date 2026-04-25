#include "sentinel/events/normalize.hpp"
#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/signal.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace sentinel::events;
using namespace sentinel::risk;

namespace {

// Verified topic0 for keccak256("AnswerUpdated(int256,uint256,uint256)").
constexpr const char* kOracleTopic0 =
    "0x0559884fd3a460db3073b7fc896cc77986f16e378210ded43186175bf646fc5f";

constexpr const char* kAggregator =
    "0x639fe6ab55c921f74e7fac1ee960c0b6293ba612"; // ETH/USD on Arbitrum

// Build a 32-byte hex string left-padded with zeros for a uint64 value.
std::string pad32_uint64(uint64_t value) {
    std::string out = "0x";
    out.reserve(2 + 64);
    // 24 zero bytes (48 hex chars), then 8 bytes of value
    for (int i = 0; i < 48; ++i) out.push_back('0');
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx",
                  static_cast<unsigned long long>(value));
    out.append(buf);
    return out;
}

} // namespace

TEST_CASE("Oracle Normalization — AnswerUpdated produces OracleUpdate signal") {
    RawLog raw{};
    raw.address = kAggregator;
    raw.topics.push_back(kOracleTopic0);
    // current = 200000000000 (e.g. $2000.00 with 8 decimals)
    raw.topics.push_back(pad32_uint64(200000000000ULL));
    // roundId = 18446744073709551615 (uint64_max for fun) — full 32-byte slot
    raw.topics.push_back(pad32_uint64(0xFFFFFFFFFFFFFFFFULL));

    // updatedAt = 1700000000 packed in the low 8 bytes of a 32-byte slot
    raw.data = pad32_uint64(1700000000ULL);
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x1";
    raw.logIndex = "0x1";
    raw.transactionHash =
        "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, /*chain_id=*/42161, /*block_timestamp=*/1700000000000ULL);

    REQUIRE(out.type == SignalType::OracleUpdate);

    const auto* payload = std::get_if<OracleUpdateEvent>(&out.payload);
    REQUIRE(payload != nullptr);

    REQUIRE(payload->chain_id == 42161);

    // Aggregator address bytes match
    std::array<uint8_t, 20> expected_addr{};
    sentinel::events::utils::parse_hex_bytes(kAggregator, expected_addr);
    REQUIRE(payload->aggregator_address == expected_addr);

    // current_answer is 200000000000 in the low 8 bytes of the 32-byte slot
    uint64_t current = 0;
    for (int i = 24; i < 32; ++i) {
        current = (current << 8) | payload->current_answer[i];
    }
    REQUIRE(current == 200000000000ULL);

    // round_id all-ones in the low 8 bytes
    uint64_t round_low = 0;
    for (int i = 24; i < 32; ++i) {
        round_low = (round_low << 8) | payload->round_id[i];
    }
    REQUIRE(round_low == 0xFFFFFFFFFFFFFFFFULL);

    REQUIRE(payload->updated_at == 1700000000ULL);
}

TEST_CASE("Oracle Normalization — updated_at extraction is big-endian from data[24..32]") {
    RawLog raw{};
    raw.address = kAggregator;
    raw.topics.push_back(kOracleTopic0);
    raw.topics.push_back(pad32_uint64(100ULL));
    raw.topics.push_back(pad32_uint64(7ULL));

    // updatedAt = 0x0102030405060708 — distinct bytes so any ordering bug is visible
    raw.data = "0x000000000000000000000000000000000000000000000000"
               "0102030405060708";
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x1";
    raw.logIndex = "0x1";
    raw.transactionHash =
        "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 1234567890);

    REQUIRE(out.type == SignalType::OracleUpdate);
    const auto* payload = std::get_if<OracleUpdateEvent>(&out.payload);
    REQUIRE(payload != nullptr);
    REQUIRE(payload->updated_at == 0x0102030405060708ULL);
}

TEST_CASE("Oracle Normalization — different topic0 is not OracleUpdate") {
    RawLog raw{};
    raw.address = kAggregator;
    // ERC-20 Transfer topic, not the oracle topic
    raw.topics.push_back(
        "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef");
    raw.topics.push_back(
        "0x000000000000000000000000aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    raw.topics.push_back(
        "0x000000000000000000000000bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");

    raw.data = pad32_uint64(1000ULL);
    raw.blockNumber = "0x100";
    raw.transactionIndex = "0x1";
    raw.logIndex = "0x1";
    raw.transactionHash =
        "0xabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd";

    Signal out;
    normalize(raw, out, 1, 1234567890);

    REQUIRE(out.type != SignalType::OracleUpdate);
    REQUIRE(std::get_if<OracleUpdateEvent>(&out.payload) == nullptr);
}
