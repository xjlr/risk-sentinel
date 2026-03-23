#include "sentinel/risk/console_alert_channel.hpp"
#include <iostream>

namespace sentinel::risk {

void ConsoleAlertChannel::send(const Alert &alert) {
  std::cout << "[AlertDispatcher] Executing Webhook for: " << alert.message
            << " [Time: " << alert.timestamp_ms << "]";
  if (alert.amount_decimal && !alert.amount_decimal->empty()) {
    std::cout << " [Amount: " << *alert.amount_decimal << "]";
  }
  if (alert.token_address && !alert.token_address->empty()) {
    std::cout << " [Token: " << *alert.token_address << "]";
  }
  std::cout << "\n";
}

} // namespace sentinel::risk
