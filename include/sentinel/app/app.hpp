#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "sentinel/chains/arbitrum/ArbitrumAdapter.hpp"
#include "sentinel/events/EventSource.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/risk/approval_config.hpp"
#include "sentinel/risk/governance_config.hpp"
#include "sentinel/risk/mint_burn_config.hpp"
#include "sentinel/risk/risk_engine.hpp"
#include "sentinel/risk/rules/large_transfer_rule.hpp"
#include "sentinel/risk/signal.hpp"
#include "sentinel/risk/webhook_alert_channel.hpp"
#include "sentinel/rpc/JsonRpcClient.hpp"
#include "sentinel/metrics/metrics.hpp"

namespace pqxx {
class connection;
}

namespace sentinel::app {

struct AppConfig {
  std::string chain;
  std::string database_url;
  std::string rpc_url;
  sentinel::events::EventSourceConfig event_source_cfg;
  bool debug = false;
  std::string readiness_file = "/tmp/sentinel.ready";
  std::chrono::milliseconds shutdown_drain_timeout{5000};
  std::string metrics_listen_address = "0.0.0.0:8080";
};

class App {
public:
  explicit App(AppConfig cfg);
  ~App();

  // Prevent copy/move
  App(const App &) = delete;
  App &operator=(const App &) = delete;

  // Blocks until shutdown is requested
  int run();

  // Can be called from signal handler / main
  void request_stop();

private:
  bool init_logging_();
  bool init_db_();
  void init_modules_();
  std::vector<sentinel::risk::LargeTransferRuleConfig>
  load_large_transfer_configs_();
  void load_governance_configs_();
  void load_mint_burn_configs_();
  void load_approval_configs_();
  void load_webhook_channels_();
  void load_customer_map_();
  void load_token_map_();
  void register_rules_();
  void start_threads_();
  void stop_orderly_();
  void join_threads_();
  void write_readiness_file_();
  void remove_readiness_file_();

  AppConfig cfg_;

  std::unordered_map<sentinel::risk::CustomerId, std::string>
      customer_id_to_key_;
  std::unordered_map<sentinel::risk::TokenKey, std::string>
      token_addresses_to_symbols_;
  std::unordered_map<sentinel::risk::GovernanceContractKey,
                     std::vector<sentinel::risk::GovernanceRuleConfig>>
      governance_rules_by_contract_;
  std::unordered_map<sentinel::risk::MintBurnContractKey,
                     std::vector<sentinel::risk::MintBurnRuleConfig>>
      mint_burn_rules_by_contract_;
  std::unordered_map<sentinel::risk::ApprovalContractKey,
                     std::vector<sentinel::risk::ApprovalRuleConfig>>
      approval_rules_by_contract_;
  std::unordered_map<std::uint64_t,
                     std::vector<sentinel::risk::WebhookEndpoint>>
      customer_webhooks_;

  std::atomic<bool> stop_requested_{false};
  std::atomic<bool> stopped_{false};
  std::mutex run_mutex_;
  std::condition_variable run_cv_;

  // Database
  std::shared_ptr<pqxx::connection> conn_;

  // Modules
  std::unique_ptr<sentinel::metrics::Metrics> metrics_;
  std::unique_ptr<sentinel::risk::RingBuffer<sentinel::risk::Signal>>
      ring_buffer_;
  std::unique_ptr<JsonRpcClient> rpc_;
  std::unique_ptr<ArbitrumAdapter> arbitrum_adapter_;
  std::unique_ptr<sentinel::events::EventSource> event_source_;
  std::unique_ptr<sentinel::risk::AlertDispatcher> dispatcher_;
  std::unique_ptr<sentinel::risk::RiskEngine> risk_engine_;

  // Threads
  std::jthread event_source_thread_;
  std::jthread dispatcher_thread_;
  std::jthread risk_engine_thread_;

  // Rules ownership
  std::vector<std::unique_ptr<sentinel::risk::IRiskRule>> rules_;
};

} // namespace sentinel::app
