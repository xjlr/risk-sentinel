#pragma once

#include "sentinel/risk/alert_channel.hpp"

namespace sentinel::risk {

class ConsoleAlertChannel : public IAlertChannel {
public:
  void send(const Alert &alert) override;
};

} // namespace sentinel::risk
