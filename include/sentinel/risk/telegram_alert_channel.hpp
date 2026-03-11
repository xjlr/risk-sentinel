#pragma once

#include "sentinel/risk/alert_channel.hpp"
#include <string>

namespace sentinel::risk {

class TelegramAlertChannel : public IAlertChannel {
public:
  TelegramAlertChannel(std::string bot_token, std::string chat_id);
  ~TelegramAlertChannel() override;

  void send(const Alert &alert) override;

private:
  std::string bot_token_;
  std::string chat_id_;
};

} // namespace sentinel::risk
