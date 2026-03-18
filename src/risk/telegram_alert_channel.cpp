#include "sentinel/risk/telegram_alert_channel.hpp"
#include "sentinel/log.hpp"
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace sentinel::risk {
namespace {

size_t discard_write_cb(char *ptr, size_t size, size_t nmemb,
                        void *userdata) noexcept {
  (void)ptr;
  (void)userdata;
  return size * nmemb;
}

size_t string_write_cb(char *ptr, size_t size, size_t nmemb,
                       void *userdata) noexcept {
  if (!userdata) {
    return 0;
  }

  auto *str = static_cast<std::string *>(userdata);
  const size_t total = size * nmemb;

  try {
    str->append(ptr, total);
    return total;
  } catch (...) {
    return 0;
  }
}
} // namespace

TelegramAlertChannel::TelegramAlertChannel(
    std::string bot_token, std::string chat_id,
    const std::unordered_map<std::uint64_t, std::string> *customer_map,
    const std::unordered_map<TokenKey, std::string> *token_map)
    : bot_token_(std::move(bot_token)), chat_id_(std::move(chat_id)),
      customer_map_(customer_map), token_map_(token_map) {}

TelegramAlertChannel::~TelegramAlertChannel() = default;

void TelegramAlertChannel::send(const Alert &alert) {
  CURL *curl = curl_easy_init();
  auto &Lalert = sentinel::logger(sentinel::LogComponent::Alert);

  if (!curl) {
    Lalert.error(
        "Telegram send failed: could not initialize curl handle for chat_id={}",
        chat_id_);
    return;
  }

  std::string url =
      "https://api.telegram.org/bot" + bot_token_ + "/sendMessage";

  std::string customer_key = std::to_string(alert.customer_id);
  if (customer_map_ && customer_map_->contains(alert.customer_id)) {
    customer_key = customer_map_->at(alert.customer_id);
  }

  std::string symbol_or_contract = alert.token_address;
  if (!alert.token_address.empty()) {
    TokenKey key{alert.chain_id, alert.token_address};
    if (token_map_ && token_map_->contains(key)) {
      symbol_or_contract = token_map_->at(key);
    }
  }

  std::string text = "[Risk Sentinel Alert]\n";
  text += "Customer: " + customer_key + "\n";
  text += "Message: " + alert.message + "\n";
  if (!alert.amount_decimal.empty()) {
    text += "Amount: " + alert.amount_decimal + "\n";
    text += "Token: " + symbol_or_contract + "\n";
  }
  text += "Time: " + std::to_string(alert.timestamp_ms);

  nlohmann::json payload;
  payload["chat_id"] = chat_id_;
  payload["text"] = text;

  std::string json_str = payload.dump();

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, json_str.size());

  struct curl_slist *headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

  /*auto discard_write_cb = [](char *ptr, size_t size, size_t nmemb,
                             void *userdata) noexcept -> size_t {
    (void)ptr;
    (void)userdata;
    return size * nmemb;
  };*/
  std::string response_body;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, string_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

  // Set timeouts
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform(curl);

  if (res != CURLE_OK) {
    Lalert.error("Telegram send failed (curl_easy_perform): {} for chat_id={}",
                 curl_easy_strerror(res), chat_id_);
  } else {
    long http_code = 0;
    CURLcode info_res =
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    if (info_res != CURLE_OK) {
      Lalert.error("Telegram send failed: could not read HTTP response code "
                   "for chat_id={}: {}",
                   chat_id_, curl_easy_strerror(info_res));
    } else if (http_code < 200 || http_code >= 300) {
      Lalert.error("Telegram send failed: chat_id={}, HTTP {}, body={}",
                   chat_id_, http_code, response_body);
    } else {
      Lalert.debug("Telegram send succeeded: chat_id={}, HTTP {}, body={}",
                   chat_id_, http_code, response_body);
    }
  }

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);
}

} // namespace sentinel::risk
