#include "sentinel/backtest/backtest_app.hpp"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "sentinel/chains/arbitrum/ArbitrumAdapter.hpp"
#include "sentinel/chains/ethereum/EthereumAdapter.hpp"
#include "sentinel/log.hpp"
#include "sentinel/risk/alert_deduplicator.hpp"
#include "sentinel/risk/rules/approval_rule.hpp"
#include "sentinel/risk/rules/bridge_transfer_rule.hpp"
#include "sentinel/risk/rules/governance_rule.hpp"
#include "sentinel/risk/rules/large_transfer_rule.hpp"
#include "sentinel/risk/rules/mint_burn_rule.hpp"
#include "sentinel/risk/rules/oracle_update_rule.hpp"

namespace sentinel::backtest {

namespace {
constexpr std::size_t kRingSize = 65536;
constexpr auto kDrainTimeout = std::chrono::seconds(30);
} // namespace

BacktestApp::BacktestApp(BacktestConfig cfg, std::string rpc_url)
    : cfg_(std::move(cfg)), rpc_url_(std::move(rpc_url)) {}

BacktestApp::~BacktestApp() = default;

int BacktestApp::run() {
  sentinel::init_logging(false);
  auto &Lcore = sentinel::logger(sentinel::LogComponent::Core);

  Lcore.info(
      "Backtest starting chain={} blocks=[{}, {}] output={} max_range={}",
      cfg_.chain, cfg_.start_block, cfg_.end_block, cfg_.output_path,
      cfg_.max_block_range);

  rpc_ = std::make_unique<JsonRpcClient>(rpc_url_, cfg_.chain, nullptr);

  if (cfg_.chain == "ethereum") {
    adapter_ = std::make_unique<EthereumAdapter>(*rpc_);
  } else if (cfg_.chain == "arbitrum") {
    adapter_ = std::make_unique<ArbitrumAdapter>(*rpc_);
  } else {
    throw std::runtime_error("BacktestApp: unsupported chain '" + cfg_.chain +
                             "'");
  }

  ring_buffer_ =
      std::make_unique<sentinel::risk::RingBuffer<sentinel::risk::Signal>>(
          kRingSize);

  sentinel::events::EventSourceConfig es_cfg{};
  es_cfg.start_block = cfg_.start_block;
  es_cfg.backtest_end_block = cfg_.end_block;
  es_cfg.max_block_range = cfg_.max_block_range;

  event_source_ = std::make_unique<sentinel::events::EventSource>(
      *adapter_, *ring_buffer_, es_cfg, cfg_.chain, nullptr, nullptr);

  std::vector<std::string> active_rule_types;
  if (!cfg_.large_transfer_configs.empty())
    active_rule_types.emplace_back("large_transfer");
  if (!cfg_.governance_rules_by_contract.empty())
    active_rule_types.emplace_back("governance");
  if (!cfg_.mint_burn_rules_by_contract.empty())
    active_rule_types.emplace_back("mint_burn");
  if (!cfg_.approval_rules_by_contract.empty())
    active_rule_types.emplace_back("approval");
  if (!cfg_.bridge_configs_by_key.empty())
    active_rule_types.emplace_back("bridge_transfer");
  if (!cfg_.oracle_configs_by_feed.empty())
    active_rule_types.emplace_back("oracle_update");

  sentinel::risk::DeduplicatorConfig dedup_cfg;
  dedup_cfg.default_window_ms = 60'000;
  dedup_cfg.per_rule_window_ms = {
      {"large_transfer", 60'000},   {"governance", 3'600'000},
      {"mint_burn", 60'000},        {"approval", 300'000},
      {"bridge_transfer", 60'000},  {"oracle_update", 300'000},
  };
  dedup_cfg.cleanup_every_n_alerts = 100;

  dispatcher_ = std::make_unique<sentinel::risk::AlertDispatcher>(
      cfg_.chain, nullptr, std::move(dedup_cfg),
      std::move(active_rule_types), nullptr);

  auto file_channel =
      std::make_unique<sentinel::risk::FileAlertChannel>(cfg_.output_path);
  file_channel_ = file_channel.get();
  dispatcher_->add_channel(std::move(file_channel));

  risk_engine_ = std::make_unique<sentinel::risk::RiskEngine>(
      *ring_buffer_, *dispatcher_, cfg_.chain, nullptr, nullptr);

  if (!cfg_.large_transfer_configs.empty()) {
    auto rule = std::make_unique<sentinel::risk::LargeTransferRule>(
        cfg_.large_transfer_configs);
    risk_engine_->register_rule(rule.get());
    rules_.push_back(std::move(rule));
  }
  if (!cfg_.governance_rules_by_contract.empty()) {
    auto rule = std::make_unique<sentinel::risk::GovernanceRule>(
        cfg_.governance_rules_by_contract);
    risk_engine_->register_rule(rule.get());
    rules_.push_back(std::move(rule));
  }
  if (!cfg_.mint_burn_rules_by_contract.empty()) {
    auto rule = std::make_unique<sentinel::risk::MintBurnRule>(
        cfg_.mint_burn_rules_by_contract);
    risk_engine_->register_rule(rule.get());
    rules_.push_back(std::move(rule));
  }
  if (!cfg_.approval_rules_by_contract.empty()) {
    auto rule = std::make_unique<sentinel::risk::ApprovalRule>(
        cfg_.approval_rules_by_contract);
    risk_engine_->register_rule(rule.get());
    rules_.push_back(std::move(rule));
  }
  if (!cfg_.bridge_configs_by_key.empty()) {
    auto rule = std::make_unique<sentinel::risk::BridgeTransferRule>(
        cfg_.bridge_configs_by_key, cfg_.bridge_addresses,
        cfg_.bridge_names);
    risk_engine_->register_rule(rule.get());
    rules_.push_back(std::move(rule));
  }
  if (!cfg_.oracle_configs_by_feed.empty()) {
    auto rule = std::make_unique<sentinel::risk::OracleUpdateRule>(
        cfg_.oracle_configs_by_feed);
    risk_engine_->register_rule(rule.get());
    rules_.push_back(std::move(rule));
  }

  if (rules_.empty()) {
    Lcore.warn("Backtest started with zero rules — no alerts will fire");
  }

  std::jthread dispatcher_thread([this](std::stop_token st) {
    dispatcher_->run(st);
  });
  std::jthread risk_engine_thread([this](std::stop_token st) {
    risk_engine_->run(st);
  });
  std::jthread event_source_thread([this](std::stop_token st) {
    event_source_->run(st);
  });

  event_source_thread.join();
  Lcore.info("EventSource thread exited; draining pipeline...");

  sentinel::risk::Signal poison_pill{};
  poison_pill.type = sentinel::risk::SignalType::Control;
  poison_pill.payload = sentinel::risk::ControlSignal{
      sentinel::risk::ControlSignal::Command::Stop};

  const auto push_deadline = std::chrono::steady_clock::now() + kDrainTimeout;
  bool pushed = false;
  while (std::chrono::steady_clock::now() < push_deadline) {
    if (ring_buffer_->try_push(poison_pill)) {
      pushed = true;
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  if (!pushed) {
    Lcore.warn("Failed to push Stop poison pill before timeout");
  }

  const auto drain_deadline =
      std::chrono::steady_clock::now() + kDrainTimeout;
  while (std::chrono::steady_clock::now() < drain_deadline) {
    if (risk_engine_->is_finished()) break;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  if (!risk_engine_->is_finished()) {
    Lcore.warn("RiskEngine did not finish in time; force stopping");
  }

  risk_engine_->stop();
  risk_engine_thread.join();

  dispatcher_->stop();
  dispatcher_thread.join();

  const std::size_t alert_count =
      file_channel_ ? file_channel_->alert_count() : 0;

  Lcore.info("Backtest complete. {} alerts written to {}", alert_count,
             cfg_.output_path);
  std::cout << "Backtest complete. " << alert_count
            << " alerts written to " << cfg_.output_path << "\n";
  return 0;
}

} // namespace sentinel::backtest
