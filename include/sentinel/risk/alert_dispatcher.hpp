#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace sentinel::metrics {
struct Metrics;
}

namespace prometheus {
class Counter;
class Gauge;
class Histogram;
}

namespace sentinel::risk {

class IAlertChannel;

using CustomerId = std::uint64_t;

struct TokenKey {
  uint64_t chain_id;
  std::string token_address;

  bool operator==(const TokenKey &other) const {
    return chain_id == other.chain_id && token_address == other.token_address;
  }
};

struct Alert {
  CustomerId customer_id;
  std::string rule_type;
  std::string message;
  uint64_t timestamp_ms;
  uint64_t internal_ingress_time_ms = 0;
  std::optional<std::string> amount_decimal;
  std::optional<std::string> token_address;
  std::optional<uint64_t> chain_id;
};

class AlertDispatcher {
public:
  AlertDispatcher(std::string chain_name, sentinel::metrics::Metrics* metrics = nullptr);
  ~AlertDispatcher();

  // Prevent copy/move
  AlertDispatcher(const AlertDispatcher &) = delete;
  AlertDispatcher &operator=(const AlertDispatcher &) = delete;

  void add_channel(std::unique_ptr<IAlertChannel> channel);

  void run(std::stop_token st = {});
  void stop();

  void dispatch(Alert alert);

private:
  std::vector<std::unique_ptr<IAlertChannel>> channels_;
  std::queue<Alert> queue_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> running_{false};
  std::string chain_name_;
  sentinel::metrics::Metrics* metrics_;

  std::unordered_map<std::string, prometheus::Counter*> alerts_sent_counters_;
  std::unordered_map<std::string, prometheus::Counter*> alerts_send_failures_counters_;

  prometheus::Gauge* last_alert_success_gauge_ = nullptr;
  prometheus::Gauge* alert_queue_depth_gauge_ = nullptr;
  prometheus::Histogram* alert_send_duration_hist_ = nullptr;
  prometheus::Histogram* signal_to_alert_hist_ = nullptr;

};

} // namespace sentinel::risk

namespace std {
template <> struct hash<sentinel::risk::TokenKey> {
  std::size_t operator()(const sentinel::risk::TokenKey &key) const {
    return std::hash<uint64_t>()(key.chain_id) ^
           (std::hash<std::string>()(key.token_address) << 1);
  }
};
} // namespace std
