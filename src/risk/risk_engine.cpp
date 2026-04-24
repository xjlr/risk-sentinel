#include "sentinel/risk/risk_engine.hpp"
#include "sentinel/health/heartbeat.hpp"
#include "sentinel/metrics/metrics.hpp"

#include <chrono>

namespace sentinel::risk {

RiskEngine::RiskEngine(RingBuffer<Signal> &input_queue,
                       AlertDispatcher &dispatcher,
                       std::string chain_name,
                       sentinel::metrics::Metrics* metrics,
                       sentinel::health::Heartbeat* heartbeat)
    : input_queue_(input_queue), dispatcher_(dispatcher), chain_name_(std::move(chain_name)),
      metrics_(metrics), heartbeat_(heartbeat) {
    if (metrics_) {
        ring_buffer_depth_gauge_ = metrics_->ring_buffer_depth_chain;
    }
}

RiskEngine::~RiskEngine() { stop(); }

void RiskEngine::register_rule(IRiskRule *rule) {
  SignalMask interests = rule->interests();
  for (std::size_t i = 0; i < SignalTypeCount; ++i) {
    if (interests & (1 << i)) {
      routing_table_[i].push_back(rule);
    }
  }
  if (metrics_) {
    std::string type_name{rule->rule_type_name()};
    if (alerts_generated_counters_.find(type_name) == alerts_generated_counters_.end()) {
      alerts_generated_counters_[type_name] =
          &metrics_->alerts_generated_total.Add({{"chain", chain_name_}, {"rule", type_name}});
    }
  }
}

void RiskEngine::stop() { running_.store(false, std::memory_order_relaxed); }

void RiskEngine::run(std::stop_token st) {
  // Pre-allocate alerts vector to avoid heap allocations in the hot path
  std::vector<Alert> alerts;
  alerts.reserve(64);

  bool drain_mode = false;

  while (running_ && !st.stop_requested()) {
    if (heartbeat_) heartbeat_->record();
    // Read from lock-free queue
    auto *signal_ptr = input_queue_.front();
    if (signal_ptr) {
      if (ring_buffer_depth_gauge_) ring_buffer_depth_gauge_->Decrement();
      // We have a signal, process it!
      Signal signal = std::move(*signal_ptr);
      input_queue_.pop();

      // Check if it's a poison pill
      if (signal.type == SignalType::Control) {
        if (std::holds_alternative<ControlSignal>(signal.payload)) {
          if (std::get<ControlSignal>(signal.payload).command ==
              ControlSignal::Command::Stop) {
            // Initiate drain phase
            drain_mode = true;
            // No more rules routing for ControlSignal MVP
            continue;
          }
        }
      }

      alerts.clear();

      // Spec 7: Reorg handling occurs before engine.
      // Engine sees signal.meta.is_final = true or a ControlSignal.
      // Engine must not implement rollback logic in MVP.

      // Spec 5.2: Routing - lookup by signal.type
      uint8_t type_idx = static_cast<uint8_t>(signal.type);

      if (type_idx < SignalTypeCount) {
        // Execute only the rules matching the signal type
        for (IRiskRule *rule : routing_table_[type_idx]) {
          // Rule evaluation is single-threaded, cache-friendly
          rule->evaluate(signal, state_store_, alerts);
        }
      }

      // Push alerts to Dispatcher Thread
      for (const auto &alert : alerts) {
        auto it = alerts_generated_counters_.find(alert.rule_type);
        if (it != alerts_generated_counters_.end()) it->second->Increment();
        dispatcher_.dispatch(alert);
      }
    } else {
      if (drain_mode) {
        // Queue is completely empty and we are draining, clean exit
        break;
      }
      // Queue is empty, yield to save CPU
      std::this_thread::yield();
    }
  }
  finished_.store(true, std::memory_order_release);
}

} // namespace sentinel::risk
