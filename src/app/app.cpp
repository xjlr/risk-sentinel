#include "sentinel/app/app.hpp"

#include <fstream>
#include <stdexcept>

#include <pqxx/pqxx>

#include "sentinel/db_checkpoint_store.hpp"
#include "sentinel/events/utils/hex.hpp"
#include "sentinel/log.hpp"
#include "sentinel/risk/rules/example_rule.hpp"
#include "sentinel/risk/rules/large_transfer_rule.hpp"
#include "sentinel/version.hpp"

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

  rpc_ = std::make_unique<JsonRpcClient>(cfg_.rpc_url);
  arbitrum_adapter_ = std::make_unique<ArbitrumAdapter>(*rpc_);
  event_source_ = std::make_unique<sentinel::events::EventSource>(
      *arbitrum_adapter_, *ring_buffer_, cfg_.event_source_cfg);

  dispatcher_ = std::make_unique<sentinel::risk::AlertDispatcher>();
  risk_engine_ =
      std::make_unique<sentinel::risk::RiskEngine>(*ring_buffer_, *dispatcher_);
}

void App::register_rules_() {
  // Register Example Rule
  auto example_rule = std::make_unique<sentinel::risk::ExampleLargeSwapRule>();
  risk_engine_->register_rule(example_rule.get());
  rules_.push_back(std::move(example_rule));

  // Register LargeTransferRule
  // USDT Arbitrum address: 0xFd086bC7CD5C481DCC9C85ebE478A1C0b69FCbb9
  std::array<uint8_t, 20> usdt_arb = {0xfd, 0x08, 0x6b, 0xc7, 0xcd, 0x5c, 0x48,
                                      0x1d, 0xcc, 0x9c, 0x85, 0xeb, 0xe4, 0x78,
                                      0xa1, 0xc0, 0xb6, 0x9f, 0xcb, 0xb9};

  // Dummy threshold: 10,000,000,000 (USDT has 6 decimals, so 10,000 USDT)
  auto threshold = sentinel::events::utils::to_be_256(10'000'000'000ULL);
  // auto threshold = sentinel::events::utils::to_be_256(100'000'000ULL);

  auto large_transfer_rule =
      std::make_unique<sentinel::risk::LargeTransferRule>(usdt_arb, threshold);
  risk_engine_->register_rule(large_transfer_rule.get());
  rules_.push_back(std::move(large_transfer_rule));
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
