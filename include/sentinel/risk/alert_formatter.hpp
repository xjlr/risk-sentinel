#pragma once

#include "sentinel/risk/alert_dispatcher.hpp"
#include <string>
#include <unordered_map>

namespace sentinel::risk {

class AlertFormatter {
public:
  // Formats an alert for Telegram channels
  static std::string format_telegram(
      const Alert &alert,
      const std::unordered_map<std::uint64_t, std::string> *customer_map,
      const std::unordered_map<TokenKey, std::string> *token_map);

  // Formats an alert for Console channels
  static std::string format_console(const Alert &alert);
};

} // namespace sentinel::risk
