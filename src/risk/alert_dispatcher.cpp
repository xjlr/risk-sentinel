#include "sentinel/risk/alert_dispatcher.hpp"
#include <iostream>

namespace sentinel::risk {

AlertDispatcher::AlertDispatcher() = default;

AlertDispatcher::~AlertDispatcher() { stop(); }

void AlertDispatcher::stop() {
  running_.store(false, std::memory_order_relaxed);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cv_.notify_all();
  }
}

void AlertDispatcher::dispatch(Alert alert) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push(std::move(alert));
  cv_.notify_one();
}

void AlertDispatcher::run(std::stop_token st) {
  running_.store(true, std::memory_order_relaxed);
  while (running_ && !st.stop_requested()) {
    Alert alert;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this, &st]() {
        return !queue_.empty() || !running_ || st.stop_requested();
      });

      if (!running_ && queue_.empty()) {
        break;
      }

      alert = std::move(queue_.front());
      queue_.pop();
    }

    // Spec 2.1: Performs HTTP/webhook I/O and may rate-limit
    // Mock HTTP/webhook I/O output to std::cout
    std::cout << "[AlertDispatcher] Executing Webhook for: " << alert.message
              << " [Time: " << alert.timestamp_ms << "]\n";
  }
}

} // namespace sentinel::risk
