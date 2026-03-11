#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/log.hpp"
#include "sentinel/risk/alert_channel.hpp"
#include <cassert>
#include <exception>

namespace sentinel::risk {

AlertDispatcher::AlertDispatcher() = default;

AlertDispatcher::~AlertDispatcher() { stop(); }

// Please note: currently this function is not thread-safe and should only be
// called from the main thread.
void AlertDispatcher::add_channel(std::unique_ptr<IAlertChannel> channel) {
  assert(!running_.load(std::memory_order_relaxed));
  if (channel) {
    channels_.push_back(std::move(channel));
  }
}

void AlertDispatcher::stop() {
  running_.store(false, std::memory_order_relaxed);
  cv_.notify_all();
}

void AlertDispatcher::dispatch(Alert alert) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push(std::move(alert));
  cv_.notify_one();
}

void AlertDispatcher::run(std::stop_token st) {
  running_.store(true, std::memory_order_relaxed);
  while (true) {
    Alert alert;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this, &st]() {
        return !queue_.empty() || !running_.load(std::memory_order_relaxed) ||
               st.stop_requested();
      });

      bool is_shutdown =
          !running_.load(std::memory_order_relaxed) || st.stop_requested();

      if (queue_.empty() && is_shutdown) {
        break;
      }

      if (queue_.empty()) {
        continue;
      }

      alert = std::move(queue_.front());
      queue_.pop();
    }

    for (const auto &channel : channels_) {
      if (channel) {
        try {
          channel->send(alert);
        } catch (const std::exception &e) {
          sentinel::logger(sentinel::LogComponent::Alert)
              .error("Exception in alert channel send: {}", e.what());
        } catch (...) {
          sentinel::logger(sentinel::LogComponent::Alert)
              .error("Unknown exception in alert channel send");
        }
      }
    }
  }
}

} // namespace sentinel::risk
