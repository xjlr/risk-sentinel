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

struct Alert {
  std::string message;
  uint64_t timestamp_ms;
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
