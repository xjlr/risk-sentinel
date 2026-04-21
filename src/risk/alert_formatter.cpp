#include "sentinel/risk/alert_formatter.hpp"

namespace sentinel::risk {

std::string AlertFormatter::format_telegram(
    const Alert &alert,
    const std::unordered_map<std::uint64_t, std::string> *customer_map,
    const std::unordered_map<TokenKey, std::string> *token_map) {

  std::string customer_key = std::to_string(alert.customer_id);
  if (customer_map && customer_map->contains(alert.customer_id)) {
    customer_key = customer_map->at(alert.customer_id);
  }

  std::string text = "[Risk Sentinel Alert]\n";
  text += "Customer: " + customer_key + "\n";
  
  if (alert.rule_type == "governance") {
    text += "Type: Governance\n";
    text += "Message: " + alert.message + "\n";
  } else if (alert.rule_type == "mint_burn") {
    text += "Type: Mint/Burn\n";
    text += "Message: " + alert.message + "\n";
    if (alert.chain_id) {
      text += "Chain ID: " + std::to_string(*alert.chain_id) + "\n";
    }
    if (alert.amount_decimal && !alert.amount_decimal->empty()) {
      text += "Amount: " + *alert.amount_decimal + "\n";
    }
    if (alert.token_address && !alert.token_address->empty()) {
      std::string symbol_or_contract = *alert.token_address;
      if (alert.chain_id) {
        TokenKey key{*alert.chain_id, *alert.token_address};
        if (token_map && token_map->contains(key)) {
          symbol_or_contract = token_map->at(key) + " (" + *alert.token_address + ")";
        }
      }
      text += "Token: " + symbol_or_contract + "\n";
    }
  } else {
    text += "Message: " + alert.message + "\n";
    if (alert.amount_decimal && !alert.amount_decimal->empty()) {
      std::string symbol_or_contract = alert.token_address.value_or("");
      if (alert.token_address && !alert.token_address->empty() && alert.chain_id) {
        TokenKey key{*alert.chain_id, *alert.token_address};
        if (token_map && token_map->contains(key)) {
          symbol_or_contract = token_map->at(key);
        }
      }

      text += "Amount: " + *alert.amount_decimal + "\n";
      text += "Token: " + symbol_or_contract + "\n";
    }
  }

  text += "Time: " + std::to_string(alert.timestamp_ms);
  return text;
}

std::string AlertFormatter::format_console(const Alert &alert) {
  std::string out = "[AlertDispatcher] Executing Webhook for: " + alert.message +
                    " [Time: " + std::to_string(alert.timestamp_ms) + "]";

  if (alert.rule_type != "governance") {
    if (alert.amount_decimal && !alert.amount_decimal->empty()) {
      out += " [Amount: " + *alert.amount_decimal + "]";
    }
    if (alert.token_address && !alert.token_address->empty()) {
      out += " [Token: " + *alert.token_address + "]";
    }
  }

  out += "\n";
  return out;
}

} // namespace sentinel::risk
