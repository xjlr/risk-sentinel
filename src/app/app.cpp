#include "sentinel/app/app.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

#include <pqxx/pqxx>

#include "sentinel/db_checkpoint_store.hpp"
#include "sentinel/events/utils/hex.hpp"
#include "sentinel/log.hpp"
#include "sentinel/risk/console_alert_channel.hpp"
#include "sentinel/risk/rules/large_transfer_rule.hpp"
#include "sentinel/risk/rules/governance_rule.hpp"
#include "sentinel/risk/telegram_alert_channel.hpp"
#include "sentinel/version.hpp"
#include "sentinel/risk/rules/mint_burn_rule.hpp"

#include <nlohmann/json.hpp>

#ifdef __linux__
#include <pthread.h>
#endif

#include <cstdio>

namespace sentinel::app {

namespace {
void set_thread_name(const char *name) {
#ifdef __linux__
  pthread_setname_np(pthread_self(), name);
#else
  (void)name;
#endif
}
} // namespace

App::App(AppConfig cfg) : cfg_(std::move(cfg)) {}

App::~App() {
  stop_orderly_();
  join_threads_();
}

int App::run() {
  if (!init_logging_()) {
    return 1;
  }

  auto &Lcore = sentinel::logger(sentinel::LogComponent::Core);
  Lcore.info("sentinel starting version={}", sentinel::kVersion);

  try {
    if (!init_db_()) {
      return 2;
    }

    init_modules_();
    register_rules_();

    start_threads_();
    write_readiness_file_();

    struct ReadinessCleanup {
      App *app;
      ~ReadinessCleanup() { app->remove_readiness_file_(); }
    } readiness_cleanup{this};

    // Block until request_stop is called
    {
      std::unique_lock<std::mutex> lock(run_mutex_);
      run_cv_.wait(lock, [this] {
        return stop_requested_.load(std::memory_order_acquire);
      });
    }

    Lcore.info("Shutting down...");

  } catch (const std::exception &e) {
    Lcore.critical("Fatal exception during execution: {}", e.what());
    stop_orderly_();
    join_threads_();
    return 3;
  } catch (...) {
    Lcore.critical("Unknown fatal exception during execution.");
    stop_orderly_();
    join_threads_();
    return 4;
  }

  stop_orderly_();
  join_threads_();

  Lcore.info("Shutdown complete.");
  return 0;
}

void App::request_stop() {
  stop_requested_.store(true, std::memory_order_release);
  run_cv_.notify_all();
}

bool App::init_logging_() {
  sentinel::init_logging(cfg_.debug);
  return true;
}

bool App::init_db_() {
  auto &Ldb = sentinel::logger(sentinel::LogComponent::Db);

  if (cfg_.database_url.empty()) {
    Ldb.critical("DATABASE_URL is empty");
    return false;
  }

  int retries = 15;
  while (retries > 0) {
    try {
      conn_ = std::make_shared<pqxx::connection>(cfg_.database_url);
      if (conn_->is_open()) {
        break;
      }
    } catch (const std::exception &e) {
      Ldb.warn("Database connection failed ({} retries left): {}", retries - 1,
               e.what());
    }
    retries--;
    if (retries > 0) {
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  if (!conn_ || !conn_->is_open()) {
    Ldb.critical("Failed to connect to database after all retries.");
    return false;
  }

  Ldb.info("database connection OK");

  sentinel::DbCheckpointStore store(conn_);

  Ldb.info("ensuring schema");
  try {
    store.ensure_schema();
  } catch (const std::exception &e) {
    Ldb.critical("ensure_schema failed: {}", e.what());
    return false;
  }

  Ldb.info("loading checkpoint");
  std::uint64_t last = 0;
  try {
    last = store.get_or_init_checkpoint(cfg_.chain);
  } catch (const std::exception &e) {
    Ldb.critical("get_or_init_checkpoint failed: {}", e.what());
    return false;
  }

  Ldb.info("checkpoint loaded chain={} last_block={}", cfg_.chain, last);
  cfg_.event_source_cfg.start_block = last;

  return true;
}

void App::init_modules_() {
  auto &Lrpc = sentinel::logger(sentinel::LogComponent::Rpc);

  constexpr std::size_t RING_SIZE = 65536;
  ring_buffer_ =
      std::make_unique<sentinel::risk::RingBuffer<sentinel::risk::Signal>>(
          RING_SIZE);

  if (cfg_.rpc_url.empty()) {
    Lrpc.critical("RPC_URL is empty");
    throw std::runtime_error("RPC_URL is empty");
  }

  metrics_ = std::make_unique<sentinel::metrics::Metrics>(cfg_.metrics_listen_address, cfg_.chain);

  rpc_ = std::make_unique<JsonRpcClient>(cfg_.rpc_url, cfg_.chain, metrics_.get());
  arbitrum_adapter_ = std::make_unique<ArbitrumAdapter>(*rpc_);
  event_source_ = std::make_unique<sentinel::events::EventSource>(
      *arbitrum_adapter_, *ring_buffer_, cfg_.event_source_cfg, cfg_.chain, metrics_.get());

  load_customer_map_();
  load_token_map_();
  load_governance_configs_();
  load_mint_burn_configs_();

  dispatcher_ = std::make_unique<sentinel::risk::AlertDispatcher>(cfg_.chain, metrics_.get());
  dispatcher_->add_channel(
      std::make_unique<sentinel::risk::ConsoleAlertChannel>());

  // Telegram alert channel can be registered here if token/chat_id are provided
  // in config
  if (const char *bot_token = std::getenv("TELEGRAM_BOT_TOKEN"); bot_token) {
    if (const char *chat_id = std::getenv("TELEGRAM_CHAT_ID"); chat_id) {
      dispatcher_->add_channel(
          std::make_unique<sentinel::risk::TelegramAlertChannel>(
              bot_token, chat_id, &customer_id_to_key_,
              &token_addresses_to_symbols_));
    }
  }

  risk_engine_ =
      std::make_unique<sentinel::risk::RiskEngine>(*ring_buffer_, *dispatcher_, cfg_.chain, metrics_.get());
}

void App::register_rules_() {
  // Register LargeTransferRule
  auto configs = load_large_transfer_configs_();
  if (configs.empty()) {
    auto &Lcore = sentinel::logger(sentinel::LogComponent::Core);
    Lcore.warn("No large_transfer configurations loaded from DB");
  }

  auto large_transfer_rule =
      std::make_unique<sentinel::risk::LargeTransferRule>(std::move(configs));
  risk_engine_->register_rule(large_transfer_rule.get());
  rules_.push_back(std::move(large_transfer_rule));

  auto governance_rule =
      std::make_unique<sentinel::risk::GovernanceRule>(governance_rules_by_contract_);
  risk_engine_->register_rule(governance_rule.get());
  rules_.push_back(std::move(governance_rule));

  auto mint_burn_rule =
      std::make_unique<sentinel::risk::MintBurnRule>(
          mint_burn_rules_by_contract_);
  risk_engine_->register_rule(mint_burn_rule.get());
  rules_.push_back(std::move(mint_burn_rule));
}

std::vector<sentinel::risk::LargeTransferRuleConfig>
App::load_large_transfer_configs_() {
  auto &Ldb = sentinel::logger(sentinel::LogComponent::Db);
  std::vector<sentinel::risk::LargeTransferRuleConfig> configs;

  try {
    pqxx::work tx(*conn_);

    std::string query = R"(
      SELECT
        c.id as customer_id,
        r.params_jsonb
      FROM customer_risk_rules r
      JOIN customers c ON r.customer_id = c.id
      WHERE r.rule_type = 'large_transfer'
        AND r.enabled = true
    )";

    pqxx::result res = tx.exec(query);

    for (const auto &row : res) {
      uint64_t customer_id = row["customer_id"].as<uint64_t>();
      std::string params_str = row["params_jsonb"].as<std::string>();

      try {
        nlohmann::json params = nlohmann::json::parse(params_str);

        uint64_t chain_id =
            params.value("chain_id", 42161ULL); // default to Arbitrum

        std::string token_str = params.value(
            "token_address", "0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9");
        std::array<uint8_t, 20> token_address{};
        sentinel::events::utils::parse_hex_bytes(token_str, token_address);

        std::string threshold_str =
            params.value("threshold", "10000000000"); // default 10k USDT
        std::array<uint8_t, 32> threshold_be =
            sentinel::events::utils::decimal_to_be_256(threshold_str);

        configs.push_back({.customer_id = customer_id,
                           .chain_id = chain_id,
                           .token_address = token_address,
                           .threshold_be = threshold_be});

        Ldb.info("Loaded large_transfer rule for customer_id: {}", customer_id);

      } catch (const std::exception &e) {
        Ldb.warn("Failed to parse params_jsonb for customer_id '{}': {}",
                 customer_id, e.what());
      }
    }

    tx.commit();
  } catch (const std::exception &e) {
    Ldb.error("Error loading large_transfer configurations: {}", e.what());
  }

  return configs;
}

void App::load_governance_configs_() {
  auto &Ldb = sentinel::logger(sentinel::LogComponent::Db);

  try {
    pqxx::work tx(*conn_);

    std::string query = R"(
      SELECT customer_id, chain_id, contract_address, enabled
      FROM customer_governance_rules
      WHERE enabled = true
    )";

    pqxx::result res = tx.exec(query);
    size_t count = 0;

    for (const auto &row : res) {
      uint64_t customer_id = row["customer_id"].as<uint64_t>();
      uint64_t chain_id = row["chain_id"].as<uint64_t>();
      std::string contract_address = row["contract_address"].as<std::string>();
      bool enabled = row["enabled"].as<bool>();

      std::transform(contract_address.begin(), contract_address.end(),
                     contract_address.begin(), ::tolower);

      sentinel::risk::GovernanceRuleConfig config{
          .customer_id = customer_id,
          .chain_id = chain_id,
          .contract_address = contract_address,
          .enabled = enabled,
          .action_filter = std::nullopt};

      sentinel::risk::GovernanceContractKey key{chain_id, contract_address};
      governance_rules_by_contract_[key].push_back(config);
      count++;
    }
    tx.commit();
    Ldb.info("Loaded {} governance rules", count);
  } catch (const std::exception &e) {
    Ldb.error("Error loading governance configurations: {}", e.what());
  }
}

void App::load_mint_burn_configs_() {
  auto &Ldb = sentinel::logger(sentinel::LogComponent::Db);

  try {
    pqxx::work tx(*conn_);

    std::string query = R"(
      SELECT customer_id, chain_id, contract_address, mint_threshold_raw, burn_threshold_raw, enabled
      FROM customer_mint_burn_rules
      WHERE enabled = true
    )";

    pqxx::result res = tx.exec(query);
    size_t count = 0;

    for (const auto &row : res) {
      uint64_t customer_id = row["customer_id"].as<uint64_t>();
      uint64_t chain_id = row["chain_id"].as<uint64_t>();
      std::string contract_address = row["contract_address"].as<std::string>();
      std::string mint_threshold_raw = row["mint_threshold_raw"].as<std::string>();
      std::string burn_threshold_raw = row["burn_threshold_raw"].as<std::string>();
      bool enabled = row["enabled"].as<bool>();

      std::transform(contract_address.begin(), contract_address.end(),
                     contract_address.begin(), ::tolower);

      std::array<uint8_t, 32> mint_threshold_be =
          sentinel::events::utils::decimal_to_be_256(mint_threshold_raw);
      std::array<uint8_t, 32> burn_threshold_be =
          sentinel::events::utils::decimal_to_be_256(burn_threshold_raw);

      sentinel::risk::MintBurnRuleConfig config{
          .customer_id = customer_id,
          .chain_id = chain_id,
          .contract_address = contract_address,
          .mint_threshold_be = mint_threshold_be,
          .burn_threshold_be = burn_threshold_be,
          .enabled = enabled};

      sentinel::risk::MintBurnContractKey key{chain_id, contract_address};
      mint_burn_rules_by_contract_[key].push_back(config);
      count++;
    }
    tx.commit();
    Ldb.info("Loaded {} mint_burn rules", count);
  } catch (const std::exception &e) {
    Ldb.error("Error loading mint_burn configurations: {}", e.what());
  }
}

void App::load_customer_map_() {
  auto &Ldb = sentinel::logger(sentinel::LogComponent::Db);

  try {
    pqxx::work tx(*conn_);
    std::string query = "SELECT id, customer_key FROM customers;";
    pqxx::result res = tx.exec(query);

    for (const auto &row : res) {
      uint64_t id = row["id"].as<uint64_t>();
      std::string key = row["customer_key"].as<std::string>();
      customer_id_to_key_[id] = key;
    }
    tx.commit();
    Ldb.info("Loaded {} customer keys map", customer_id_to_key_.size());
  } catch (const std::exception &e) {
    Ldb.error("Error loading customer map: {}", e.what());
  }
}

void App::load_token_map_() {
  // Populate static dummy token symbols (e.g. USDT, USDC)
  // USDT Arbitrum address: 0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9
  // (normalized lower)
  sentinel::risk::TokenKey usdt_arb{
      42161, "0xfd086bc7cd5c481dcc9c85ebe478a1c0b69fcbb9"};
  token_addresses_to_symbols_[usdt_arb] = "USDT";
}

void App::start_threads_() {
  auto &Lcore = sentinel::logger(sentinel::LogComponent::Core);

  Lcore.info("Starting Dispatcher thread");
  dispatcher_thread_ = std::jthread([this](std::stop_token st) {
    set_thread_name("dispatcher");
    dispatcher_->run(st);
  });

  Lcore.info("Starting RiskEngine thread");
  risk_engine_thread_ = std::jthread([this](std::stop_token st) {
    set_thread_name("risk_engine");
    risk_engine_->run(st);
  });

  Lcore.info("Starting EventSource thread");
  event_source_thread_ = std::jthread([this](std::stop_token st) {
    set_thread_name("event_source");
    event_source_->run(st);
  });
}

void App::stop_orderly_() {
  bool expected = false;
  if (!stopped_.compare_exchange_strong(expected, true)) {
    return; // Already stopped
  }

  auto &Lcore = sentinel::logger(sentinel::LogComponent::Core);

  if (event_source_) {
    Lcore.info("Stopping EventSource...");
    event_source_->stop();
  }

  if (risk_engine_ && ring_buffer_) {
    Lcore.info("Pushing ControlStop to RiskEngine...");

    // Push poison pill
    sentinel::risk::Signal poison_pill;
    poison_pill.type = sentinel::risk::SignalType::Control;
    poison_pill.payload = sentinel::risk::ControlSignal{
        sentinel::risk::ControlSignal::Command::Stop};

    // Spin until we can push it with a timeout
    auto push_start_time = std::chrono::steady_clock::now();
    bool pushed = false;
    while (std::chrono::steady_clock::now() - push_start_time <
           cfg_.shutdown_drain_timeout) {
      if (ring_buffer_->try_push(poison_pill)) {
        pushed = true;
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    if (!pushed) {
      Lcore.warn("RingBuffer full: failed to push poison pill in time! "
                 "Proceeding to force stop fallback.");
    } else {
      // Give RiskEngine time to drain
      auto start_time = std::chrono::steady_clock::now();
      bool drained = false;

      while (std::chrono::steady_clock::now() - start_time <
             cfg_.shutdown_drain_timeout) {
        if (risk_engine_->is_finished()) {
          drained = true;
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
      }

      if (!drained) {
        Lcore.warn("RiskEngine drain timed out! Force stopping.");
      }
    }

    risk_engine_->stop();
  }

  if (dispatcher_) {
    Lcore.info("Stopping AlertDispatcher...");
    dispatcher_->stop();
  }
}

void App::join_threads_() {
  auto &Lcore = sentinel::logger(sentinel::LogComponent::Core);

  if (event_source_thread_.joinable()) {
    event_source_thread_.join();
    Lcore.info("EventSource thread joined");
  }
  if (risk_engine_thread_.joinable()) {
    risk_engine_thread_.join();
    Lcore.info("RiskEngine thread joined");
  }
  if (dispatcher_thread_.joinable()) {
    dispatcher_thread_.join();
    Lcore.info("AlertDispatcher thread joined");
  }

  // Call spdlog shutdown exactly once (even if join_threads_() runs twice)
  static std::once_flag spdlog_shutdown_once;
  std::call_once(spdlog_shutdown_once, [] { spdlog::shutdown(); });
}

void App::write_readiness_file_() {
  std::ofstream f(cfg_.readiness_file);
  f << "ready\n";
  auto &Lcore = sentinel::logger(sentinel::LogComponent::Core);
  Lcore.info("sentinel is ready at {}", cfg_.readiness_file);
}

void App::remove_readiness_file_() {
  if (std::remove(cfg_.readiness_file.c_str()) != 0) {
    auto &Lcore = sentinel::logger(sentinel::LogComponent::Core);
    Lcore.warn("Failed to remove readiness file at {}", cfg_.readiness_file);
  }
}

} // namespace sentinel::app
