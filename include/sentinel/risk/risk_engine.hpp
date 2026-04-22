#pragma once

#include "alert_dispatcher.hpp"
#include "rule_interface.hpp"
#include "signal.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sentinel::metrics {
struct Metrics;
}

namespace prometheus {
class Counter;
class Gauge;
}

namespace sentinel::risk {

class RiskEngine {
public:
  explicit RiskEngine(RingBuffer<Signal> &input_queue,
                      AlertDispatcher &dispatcher,
                      std::string chain_name,
                      sentinel::metrics::Metrics* metrics = nullptr);
  ~RiskEngine();

  // Prevent copy/move
  RiskEngine(const RiskEngine &) = delete;
  RiskEngine &operator=(const RiskEngine &) = delete;

  void register_rule(IRiskRule *rule);

  void run(std::stop_token st = {});
  void stop();
  bool is_finished() const { return finished_.load(std::memory_order_acquire); }

private:
  RingBuffer<Signal> &input_queue_;
  AlertDispatcher &dispatcher_;
  StateStore state_store_;

  // Array of rule lists indexed by SignalType
  std::array<std::vector<IRiskRule *>, SignalTypeCount> routing_table_;

  std::atomic<bool> running_{true};
  std::atomic<bool> finished_{false};
  std::string chain_name_;
  sentinel::metrics::Metrics* metrics_;
  prometheus::Gauge* ring_buffer_depth_gauge_ = nullptr;
  
  std::unordered_map<std::string, prometheus::Counter*> alerts_generated_counters_;
};

} // namespace sentinel::risk
