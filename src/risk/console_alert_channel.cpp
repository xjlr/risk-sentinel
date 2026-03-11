#include "sentinel/risk/console_alert_channel.hpp"
#include <iostream>

namespace sentinel::risk {

void ConsoleAlertChannel::send(const Alert &alert) {
  std::cout << "[AlertDispatcher] Executing Webhook for: " << alert.message
            << " [Time: " << alert.timestamp_ms << "]\n";
}

} // namespace sentinel::risk
