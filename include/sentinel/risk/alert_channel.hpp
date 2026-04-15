#pragma once

#include <string>
#include "sentinel/risk/alert_dispatcher.hpp"

namespace sentinel::risk {

class IAlertChannel {
public:
  virtual ~IAlertChannel() = default;
  virtual std::string name() const = 0;
  virtual void send(const Alert &alert) = 0;
};

} // namespace sentinel::risk
