#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>

#include <pthread.h>
#include <thread>

#include "sentinel/admin/encrypt_secret.hpp"
#include "sentinel/app/app.hpp"
#include "sentinel/backtest/backtest_app.hpp"
#include "sentinel/backtest/backtest_config.hpp"

static std::string getenv_or(const char *k, const char *defv) {
  if (const char *v = std::getenv(k))
    return std::string(v);
  return std::string(defv);
}

static bool env_is_true(const char *k) {
  const char *v = std::getenv(k);
  if (!v)
    return false;
  const std::string s(v);
  return (s == "1" || s == "true" || s == "TRUE" || s == "yes" || s == "on");
}

int main(int argc, char **argv) {
  if (argc >= 3 && std::strcmp(argv[1], "admin") == 0) {
    if (std::strcmp(argv[2], "encrypt-secret") == 0)
      return sentinel::admin::encrypt_secret_command(argc, argv);
    std::cerr << "Unknown admin command: " << argv[2] << "\n";
    return 1;
  }

  if (argc >= 2 && std::strcmp(argv[1], "backtest") == 0) {
    if (argc < 3) {
      std::cerr << "Usage: sentinel backtest <config.yaml>\n";
      return 2;
    }

    const std::string ethereum_rpc = getenv_or("ETHEREUM_RPC_URL", "");
    const std::string arbitrum_rpc = getenv_or("ARBITRUM_RPC_URL", "");

    try {
      auto cfg = sentinel::backtest::load_backtest_config(argv[2]);
      const std::string rpc_url = (cfg.chain == "ethereum")
                                      ? ethereum_rpc
                                      : arbitrum_rpc;
      if (rpc_url.empty()) {
        std::cerr << "Missing RPC URL for chain " << cfg.chain
                  << " (set ETHEREUM_RPC_URL or ARBITRUM_RPC_URL)\n";
        return 3;
      }
      sentinel::backtest::BacktestApp app(std::move(cfg), rpc_url);
      return app.run();
    } catch (const std::exception &e) {
      std::cerr << "Backtest failed: " << e.what() << "\n";
      return 4;
    }
  }

  sentinel::app::AppConfig cfg;

  cfg.chain = getenv_or("CHAIN", "arbitrum");
  cfg.database_url = getenv_or("DATABASE_URL", "");
  if (cfg.chain == "ethereum") {
    cfg.rpc_url = getenv_or("ETHEREUM_RPC_URL", "");
  } else {
    cfg.rpc_url = getenv_or("ARBITRUM_RPC_URL", "");
  }
  cfg.health_listen_address = getenv_or("HEALTH_LISTEN_ADDRESS", "0.0.0.0:8081");

  const std::string log_level = getenv_or("LOG_LEVEL", "info");
  cfg.debug = (log_level == "debug") || env_is_true("DEBUG");

  cfg.event_source_cfg.max_block_range = 1000;

  sigset_t set;
  sigemptyset(&set);
  sigaddset(&set, SIGINT);
  sigaddset(&set, SIGTERM);

  if (pthread_sigmask(SIG_BLOCK, &set, nullptr) != 0) {
    return 1;
  }

  sentinel::app::App app(cfg);

  std::jthread sig_waiter_thread([&app, set](std::stop_token st) mutable {
    while (!st.stop_requested()) {
      timespec timeout{0, 200'000'000}; // 200ms
      int sig = sigtimedwait(&set, nullptr, &timeout);

      if (sig == SIGINT || sig == SIGTERM) {
        app.request_stop();
        return;
      }

      if (sig == -1) {
        if (errno == EAGAIN || errno == EINTR) {
          continue; // timeout or interrupted, check stop_token and retry
        }
        // Unexpected error -> best effort: request stop and exit
        app.request_stop();
        return;
      }
    }
  });

  return app.run();
}
