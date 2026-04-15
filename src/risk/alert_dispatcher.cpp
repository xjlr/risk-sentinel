#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/metrics/metrics.hpp"
#include "sentinel/log.hpp"
#include "sentinel/risk/alert_channel.hpp"
#include <cassert>
#include <exception>

namespace sentinel::risk {

AlertDispatcher::AlertDispatcher(std::string chain_name, sentinel::metrics::Metrics* metrics)
    : chain_name_(std::move(chain_name)), metrics_(metrics) {
    if (metrics_) {
        last_alert_success_gauge_ = metrics_->last_alert_success_timestamp_seconds_chain;
        alert_queue_depth_gauge_ = metrics_->alert_queue_depth_chain;
        alert_send_duration_hist_ = &metrics_->alert_send_duration_seconds.Add({{"chain", chain_name_}}, prometheus::Histogram::BucketBoundaries{0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0});
        signal_to_alert_hist_ = &metrics_->signal_to_alert_seconds.Add({{"chain", chain_name_}}, prometheus::Histogram::BucketBoundaries{0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0, 60.0});
    }
}

AlertDispatcher::~AlertDispatcher() { stop(); }

// Please note: currently this function is not thread-safe and should only be
// called from the main thread.
void AlertDispatcher::add_channel(std::unique_ptr<IAlertChannel> channel) {
  assert(!running_.load(std::memory_order_relaxed));
  if (channel) {
    channels_.push_back(std::move(channel));
  }
}

prometheus::Counter* AlertDispatcher::get_alerts_sent_counter(const std::string& channel) {
    if (!metrics_) return nullptr;
    auto it = alerts_sent_counters_.find(channel);
    if (it != alerts_sent_counters_.end()) return it->second;
    auto* counter = &metrics_->alerts_sent_total.Add({{"chain", chain_name_}, {"channel", channel}});
    alerts_sent_counters_[channel] = counter;
    return counter;
}

prometheus::Counter* AlertDispatcher::get_alerts_send_failures_counter(const std::string& channel) {
    if (!metrics_) return nullptr;
    auto it = alerts_send_failures_counters_.find(channel);
    if (it != alerts_send_failures_counters_.end()) return it->second;
    auto* counter = &metrics_->alerts_send_failures_total.Add({{"chain", chain_name_}, {"channel", channel}});
    alerts_send_failures_counters_[channel] = counter;
    return counter;
}

void AlertDispatcher::stop() {
  running_.store(false, std::memory_order_relaxed);
  cv_.notify_all();
}

void AlertDispatcher::dispatch(Alert alert) {
  std::lock_guard<std::mutex> lock(mutex_);
  queue_.push(std::move(alert));
  if (alert_queue_depth_gauge_) alert_queue_depth_gauge_->Set(queue_.size());
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
      if (alert_queue_depth_gauge_) alert_queue_depth_gauge_->Set(queue_.size());
    }

    auto send_start = std::chrono::steady_clock::now();
    bool any_success = false;

    for (const auto &channel : channels_) {
      if (channel) {
        try {
          channel->send(alert);
          any_success = true;
          if (auto* c = get_alerts_sent_counter(channel->name())) c->Increment();
        } catch (const std::exception &e) {
          if (auto* c = get_alerts_send_failures_counter(channel->name())) c->Increment();
          sentinel::logger(sentinel::LogComponent::Alert)
              .error("Exception in alert channel send: {}", e.what());
        } catch (...) {
          if (auto* c = get_alerts_send_failures_counter(channel->name())) c->Increment();
          sentinel::logger(sentinel::LogComponent::Alert)
              .error("Unknown exception in alert channel send");
        }
      }
    }

    if (metrics_) {
      if (any_success && last_alert_success_gauge_) {
        last_alert_success_gauge_->Set(
            static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch()).count())
        );
      }

      auto send_end = std::chrono::steady_clock::now();
      double send_duration = std::chrono::duration<double>(send_end - send_start).count();
      if (alert_send_duration_hist_) alert_send_duration_hist_->Observe(send_duration);

      if (alert.internal_ingress_time_ms > 0 && signal_to_alert_hist_) {
        double e2e_duration = (std::chrono::duration_cast<std::chrono::milliseconds>(
            send_end.time_since_epoch()).count() - alert.internal_ingress_time_ms) / 1000.0;
        signal_to_alert_hist_->Observe(e2e_duration);
      }
    }
  }
}

} // namespace sentinel::risk
