#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

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
  std::string amount_decimal;
  std::string token_address;
  uint64_t chain_id;
};

class AlertDispatcher {
public:
  AlertDispatcher();
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
