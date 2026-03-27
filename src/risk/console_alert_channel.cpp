#include "sentinel/risk/console_alert_channel.hpp"
#include "sentinel/risk/alert_formatter.hpp"
#include <iostream>

namespace sentinel::risk {

void ConsoleAlertChannel::send(const Alert &alert) {
  std::cout << AlertFormatter::format_console(alert);
}

} // namespace sentinel::risk
