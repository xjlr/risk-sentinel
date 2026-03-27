#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/metrics/metrics.hpp"
#include "sentinel/log.hpp"
#include "sentinel/risk/alert_channel.hpp"
#include <cassert>
#include <exception>

namespace sentinel::risk {

AlertDispatcher::AlertDispatcher(sentinel::metrics::Metrics* metrics) : metrics_(metrics) {}

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
  if (metrics_) metrics_->alert_queue_depth.Set(queue_.size());
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
      if (metrics_) metrics_->alert_queue_depth.Set(queue_.size());
    }

    auto send_start = std::chrono::steady_clock::now();
    bool any_success = false;

    for (const auto &channel : channels_) {
      if (channel) {
        try {
          channel->send(alert);
          any_success = true;
        } catch (const std::exception &e) {
          if (metrics_) metrics_->alerts_send_failures_total.Increment();
          sentinel::logger(sentinel::LogComponent::Alert)
              .error("Exception in alert channel send: {}", e.what());
        } catch (...) {
          if (metrics_) metrics_->alerts_send_failures_total.Increment();
          sentinel::logger(sentinel::LogComponent::Alert)
              .error("Unknown exception in alert channel send");
        }
      }
    }

    if (metrics_) {
      if (any_success) {
        metrics_->alerts_sent_total.Increment();
        metrics_->last_alert_success_timestamp_seconds.Set(
            static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())
        );
      }

      auto send_end = std::chrono::steady_clock::now();
      double send_duration = std::chrono::duration<double>(send_end - send_start).count();
      metrics_->alert_send_duration_seconds.Observe(send_duration);

      if (alert.internal_ingress_time_ms > 0) {
        double e2e_duration = (std::chrono::duration_cast<std::chrono::milliseconds>(
            send_end.time_since_epoch()).count() - alert.internal_ingress_time_ms) / 1000.0;
        metrics_->signal_to_alert_seconds.Observe(e2e_duration);
      }
    }
  }
}

} // namespace sentinel::risk
