#pragma once
#include <string>

#include "sentinel/risk/alert_channel.hpp"

namespace sentinel::risk {

class ConsoleAlertChannel : public IAlertChannel {
public:
  void send(const Alert &alert) override;
  std::string name() const override { return "console"; }
};

} // namespace sentinel::risk
