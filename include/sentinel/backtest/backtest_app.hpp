#pragma once

#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "sentinel/backtest/backtest_config.hpp"
#include "sentinel/chains/ChainAdapter.hpp"
#include "sentinel/events/EventSource.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/risk/file_alert_channel.hpp"
#include "sentinel/risk/risk_engine.hpp"
#include "sentinel/risk/rule_interface.hpp"
#include "sentinel/risk/signal.hpp"
#include "sentinel/rpc/JsonRpcClient.hpp"

namespace sentinel::backtest {

class BacktestApp {
public:
  BacktestApp(BacktestConfig cfg, std::string rpc_url);
  ~BacktestApp();

  BacktestApp(const BacktestApp &) = delete;
  BacktestApp &operator=(const BacktestApp &) = delete;

  int run();

private:
  BacktestConfig cfg_;
  std::string rpc_url_;

  std::unique_ptr<JsonRpcClient> rpc_;
  std::unique_ptr<ChainAdapter> adapter_;
  std::unique_ptr<sentinel::risk::RingBuffer<sentinel::risk::Signal>>
      ring_buffer_;
  std::unique_ptr<sentinel::events::EventSource> event_source_;
  std::unique_ptr<sentinel::risk::AlertDispatcher> dispatcher_;
  std::unique_ptr<sentinel::risk::RiskEngine> risk_engine_;
  sentinel::risk::FileAlertChannel *file_channel_ = nullptr;

  std::vector<std::unique_ptr<sentinel::risk::IRiskRule>> rules_;
};

} // namespace sentinel::backtest
