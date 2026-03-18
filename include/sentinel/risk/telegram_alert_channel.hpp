#pragma once

#include "sentinel/risk/alert_channel.hpp"
#include <string>
#include <unordered_map>

namespace sentinel::risk {

class TelegramAlertChannel : public IAlertChannel {
public:
  TelegramAlertChannel(
      std::string bot_token, std::string chat_id,
      const std::unordered_map<std::uint64_t, std::string> *customer_map,
      const std::unordered_map<TokenKey, std::string> *token_map);
  ~TelegramAlertChannel() override;

  void send(const Alert &alert) override;

private:
  std::string bot_token_;
  std::string chat_id_;
  const std::unordered_map<std::uint64_t, std::string> *customer_map_;
  const std::unordered_map<TokenKey, std::string> *token_map_;
};

} // namespace sentinel::risk
