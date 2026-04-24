#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/metrics/metrics.hpp"
#include "sentinel/log.hpp"
#include "sentinel/risk/alert_channel.hpp"
#include <cassert>
#include <chrono>
#include <exception>

namespace sentinel::risk {

AlertDispatcher::AlertDispatcher(std::string chain_name,
                                 sentinel::metrics::Metrics* metrics,
                                 DeduplicatorConfig dedup_cfg,
                                 std::vector<std::string> rule_types,
                                 sentinel::health::Heartbeat* heartbeat)
    : chain_name_(std::move(chain_name)),
      metrics_(metrics),
      heartbeat_(heartbeat),
      deduplicator_(std::move(dedup_cfg)) {
    if (metrics_) {
        last_alert_success_gauge_ = metrics_->last_alert_success_timestamp_seconds_chain;
        alert_queue_depth_gauge_ = metrics_->alert_queue_depth_chain;
        alert_send_duration_hist_ = &metrics_->alert_send_duration_seconds.Add(
            {{"chain", chain_name_}},
            prometheus::Histogram::BucketBoundaries{0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0});
        signal_to_alert_hist_ = &metrics_->signal_to_alert_seconds.Add(
            {{"chain", chain_name_}},
            prometheus::Histogram::BucketBoundaries{0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0, 30.0, 60.0});

        for (const auto& rule_type : rule_types) {
            alerts_deduplicated_counters_[rule_type] =
                &metrics_->alerts_deduplicated_total.Add(
                    {{"chain", chain_name_}, {"rule_type", rule_type}});
        }
    }
}

AlertDispatcher::~AlertDispatcher() { stop(); }

// Please note: currently this function is not thread-safe and should only be
// called from the main thread.
void AlertDispatcher::add_channel(std::unique_ptr<IAlertChannel> channel) {
  assert(!running_.load(std::memory_order_relaxed));
  if (channel) {
    if (metrics_) {
      const std::string ch_name = channel->name();
      alerts_sent_counters_[ch_name] =
          &metrics_->alerts_sent_total.Add({{"chain", chain_name_}, {"channel", ch_name}});
      alerts_send_failures_counters_[ch_name] =
          &metrics_->alerts_send_failures_total.Add({{"chain", chain_name_}, {"channel", ch_name}});
    }
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
  if (alert_queue_depth_gauge_) alert_queue_depth_gauge_->Set(queue_.size());
  cv_.notify_one();
}

void AlertDispatcher::run(std::stop_token st) {
  running_.store(true, std::memory_order_relaxed);
  while (true) {
    if (heartbeat_) heartbeat_->record();
    Alert alert;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      // The heartbeat requires this thread to wake periodically even when
      // idle, otherwise a quiet dispatcher would appear stuck to /readyz.
      // cv_.wait_for returns immediately on notify_one() from dispatch(),
      // so alert latency is unaffected — the 1-second timeout only triggers
      // when the queue has been empty for a second, which is exactly when
      // the heartbeat needs refreshing. This is a deliberate tradeoff of
      // "no loop changes" for correct liveness reporting.
      cv_.wait_for(lock, std::chrono::seconds(1), [this, &st]() {
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

    const uint64_t now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    if (deduplicator_.should_suppress(alert, now_ms)) {
      auto it = alerts_deduplicated_counters_.find(alert.rule_type);
      if (it != alerts_deduplicated_counters_.end() && it->second) {
        it->second->Increment();
      }
      continue;
    }

    auto send_start = std::chrono::steady_clock::now();
    bool any_success = false;

    for (const auto &channel : channels_) {
      if (channel) {
        try {
          channel->send(alert);
          any_success = true;
          auto it = alerts_sent_counters_.find(channel->name());
          if (it != alerts_sent_counters_.end()) it->second->Increment();
        } catch (const std::exception &e) {
          auto it = alerts_send_failures_counters_.find(channel->name());
          if (it != alerts_send_failures_counters_.end()) it->second->Increment();
          sentinel::logger(sentinel::LogComponent::Alert)
              .error("Exception in alert channel send: {}", e.what());
        } catch (...) {
          auto it = alerts_send_failures_counters_.find(channel->name());
          if (it != alerts_send_failures_counters_.end()) it->second->Increment();
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
