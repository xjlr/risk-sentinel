#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

#include "sentinel/backtest/backtest_config.hpp"

using sentinel::backtest::BacktestConfig;
using sentinel::backtest::load_backtest_config;

namespace {

std::filesystem::path unique_tmp_path() {
    static std::atomic<uint64_t> counter{0};
    const auto id = counter.fetch_add(1, std::memory_order_relaxed);
    const auto pid = static_cast<uint64_t>(::getpid());
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           ("backtest_config_test_" + std::to_string(pid) + "_" +
            std::to_string(now) + "_" + std::to_string(id) + ".yaml");
}

std::filesystem::path write_yaml(const std::string &content) {
    const auto path = unique_tmp_path();
    std::ofstream f(path);
    f << content;
    return path;
}

constexpr const char *kMinimalYaml = R"(
chain: ethereum
start_block: 100
end_block: 200
output_path: ./out.jsonl

customers:
  - id: 1
    key: "research"
    display_name: "Research"

rules:
  large_transfer: []
  governance: []
  mint_burn: []
  approval: []
  bridge_transfers: []
  oracle_update: []

bridge_contracts: []
)";

} // namespace

TEST_CASE("load_backtest_config: minimal valid YAML loads correctly",
          "[backtest_config]") {
    const auto path = write_yaml(kMinimalYaml);
    const auto cfg = load_backtest_config(path.string());
    REQUIRE(cfg.chain == "ethereum");
    REQUIRE(cfg.start_block == 100);
    REQUIRE(cfg.end_block == 200);
    REQUIRE(cfg.output_path == "./out.jsonl");
    REQUIRE(cfg.max_block_range == 1000);
    REQUIRE(cfg.customers.size() == 1);
    REQUIRE(cfg.customers[0].id == 1);
    REQUIRE(cfg.customers[0].key == "research");
    REQUIRE(cfg.large_transfer_configs.empty());
    REQUIRE(cfg.governance_rules_by_contract.empty());
    REQUIRE(cfg.oracle_configs_by_feed.empty());
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: only oracle_update populated leaves others empty",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: ethereum
start_block: 13499500
end_block: 13499600
output_path: ./out.jsonl

customers:
  - id: 1
    key: "k"
    display_name: "K"

rules:
  oracle_update:
    - customer_id: 1
      chain_id: 1
      aggregator_address: "0x0000000000000000000000000000000000000001"
      feed_label: "yUSD/USD"
      spike_threshold_bps: 500
      decimals: 8
)";
    const auto path = write_yaml(yaml);
    const auto cfg = load_backtest_config(path.string());
    REQUIRE(cfg.large_transfer_configs.empty());
    REQUIRE(cfg.governance_rules_by_contract.empty());
    REQUIRE(cfg.mint_burn_rules_by_contract.empty());
    REQUIRE(cfg.approval_rules_by_contract.empty());
    REQUIRE(cfg.bridge_configs_by_key.empty());
    REQUIRE(cfg.oracle_configs_by_feed.size() == 1);
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: all six rule sections populate maps",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: ethereum
start_block: 1
end_block: 2
output_path: ./out.jsonl

customers:
  - id: 1
    key: "k"
    display_name: "K"

rules:
  large_transfer:
    - customer_id: 1
      chain_id: 1
      token_address: "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48"
      threshold_decimal: "1000"
  governance:
    - customer_id: 1
      chain_id: 1
      contract_address: "0x0000000000000000000000000000000000000aaa"
  mint_burn:
    - customer_id: 1
      chain_id: 1
      contract_address: "0x0000000000000000000000000000000000000bbb"
      mint_threshold_decimal: "100"
      burn_threshold_decimal: "200"
  approval:
    - customer_id: 1
      chain_id: 1
      token_address: "0x0000000000000000000000000000000000000ccc"
      threshold_decimal: "300"
      alert_on_infinite: true
  bridge_transfers:
    - customer_id: 1
      chain_id: 1
      token_address: "0x0000000000000000000000000000000000000ddd"
      threshold_decimal: "400"
  oracle_update:
    - customer_id: 1
      chain_id: 1
      aggregator_address: "0x0000000000000000000000000000000000000eee"
      feed_label: "X"
      spike_threshold_bps: 250
      decimals: 8

bridge_contracts:
  - chain_id: 1
    address: "0x0000000000000000000000000000000000000fff"
    bridge_name: "TestBridge"
)";
    const auto path = write_yaml(yaml);
    const auto cfg = load_backtest_config(path.string());
    REQUIRE(cfg.large_transfer_configs.size() == 1);
    REQUIRE(cfg.governance_rules_by_contract.size() == 1);
    REQUIRE(cfg.mint_burn_rules_by_contract.size() == 1);
    REQUIRE(cfg.approval_rules_by_contract.size() == 1);
    REQUIRE(cfg.bridge_configs_by_key.size() == 1);
    REQUIRE(cfg.oracle_configs_by_feed.size() == 1);
    REQUIRE(cfg.bridge_addresses.size() == 1);
    REQUIRE(cfg.bridge_names.size() == 1);
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: missing required top-level field throws",
          "[backtest_config]") {
    const std::string yaml = R"(
start_block: 1
end_block: 2
output_path: ./out.jsonl
)";
    const auto path = write_yaml(yaml);
    REQUIRE_THROWS_AS(load_backtest_config(path.string()), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: start_block >= end_block throws",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: ethereum
start_block: 100
end_block: 100
output_path: ./out.jsonl
)";
    const auto path = write_yaml(yaml);
    REQUIRE_THROWS_AS(load_backtest_config(path.string()), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: unknown chain throws",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: solana
start_block: 1
end_block: 2
output_path: ./out.jsonl
)";
    const auto path = write_yaml(yaml);
    REQUIRE_THROWS_AS(load_backtest_config(path.string()), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: malformed hex address throws",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: ethereum
start_block: 1
end_block: 2
output_path: ./out.jsonl

customers:
  - id: 1
    key: "k"
    display_name: "K"

rules:
  large_transfer:
    - customer_id: 1
      chain_id: 1
      token_address: "0xNOT_HEX"
      threshold_decimal: "100"
)";
    const auto path = write_yaml(yaml);
    REQUIRE_THROWS_AS(load_backtest_config(path.string()), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: empty output_path throws",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: ethereum
start_block: 1
end_block: 2
output_path: ""
)";
    const auto path = write_yaml(yaml);
    REQUIRE_THROWS_AS(load_backtest_config(path.string()), std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: nonexistent file throws",
          "[backtest_config]") {
    REQUIRE_THROWS_AS(load_backtest_config("/nonexistent_backtest_config.yaml"),
                      std::runtime_error);
}

TEST_CASE("load_backtest_config: empty rule lists are silently skipped",
          "[backtest_config]") {
    const auto path = write_yaml(kMinimalYaml);
    REQUIRE_NOTHROW(load_backtest_config(path.string()));
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: arbitrum chain is accepted",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: arbitrum
start_block: 1
end_block: 2
output_path: ./out.jsonl
)";
    const auto path = write_yaml(yaml);
    const auto cfg = load_backtest_config(path.string());
    REQUIRE(cfg.chain == "arbitrum");
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: rule with undeclared customer_id throws",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: ethereum
start_block: 1
end_block: 100
output_path: ./out.jsonl
customers:
  - id: 1
    key: "test"
    display_name: "Test"
rules:
  large_transfer:
    - customer_id: 11
      chain_id: 1
      token_address: "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48"
      threshold_decimal: "1000"
)";
    const auto path = write_yaml(yaml);
    REQUIRE_THROWS_AS(load_backtest_config(path.string()),
                      std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: rules with empty customers section throw",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: ethereum
start_block: 1
end_block: 100
output_path: ./out.jsonl
customers: []
rules:
  large_transfer:
    - customer_id: 1
      chain_id: 1
      token_address: "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48"
      threshold_decimal: "1000"
)";
    const auto path = write_yaml(yaml);
    REQUIRE_THROWS_AS(load_backtest_config(path.string()),
                      std::runtime_error);
    std::filesystem::remove(path);
}

TEST_CASE("load_backtest_config: matching customer_id loads cleanly",
          "[backtest_config]") {
    const std::string yaml = R"(
chain: ethereum
start_block: 1
end_block: 100
output_path: ./out.jsonl
customers:
  - id: 1
    key: "test"
    display_name: "Test"
  - id: 2
    key: "test2"
    display_name: "Test 2"
rules:
  large_transfer:
    - customer_id: 1
      chain_id: 1
      token_address: "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48"
      threshold_decimal: "1000"
    - customer_id: 2
      chain_id: 1
      token_address: "0xdac17f958d2ee523a2206206994597c13d831ec7"
      threshold_decimal: "2000"
)";
    const auto path = write_yaml(yaml);
    const auto cfg = load_backtest_config(path.string());
    REQUIRE(cfg.large_transfer_configs.size() == 2);
    std::filesystem::remove(path);
}
