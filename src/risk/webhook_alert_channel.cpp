#include "sentinel/risk/webhook_alert_channel.hpp"

#include <chrono>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "sentinel/log.hpp"
#include "sentinel/security/crypto.hpp"

namespace sentinel::risk {

namespace {

size_t string_write_cb(char *ptr, size_t size, size_t nmemb,
                       void *userdata) noexcept {
    if (!userdata)
        return 0;
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

WebhookAlertChannel::WebhookAlertChannel(
    std::unordered_map<std::uint64_t, std::vector<WebhookEndpoint>>
        customer_webhooks)
    : customer_webhooks_(std::move(customer_webhooks)) {}

WebhookAlertChannel::~WebhookAlertChannel() = default;

void WebhookAlertChannel::send(const Alert &alert) {
    auto it = customer_webhooks_.find(alert.customer_id);
    if (it == customer_webhooks_.end() || it->second.empty())
        return;

    auto &Lalert = sentinel::logger(sentinel::LogComponent::Alert);

    // Build the JSON payload once — sign and send THESE exact bytes.
    nlohmann::json payload;
    payload["customer_id"] = alert.customer_id;
    payload["rule_type"]   = alert.rule_type;
    payload["message"]     = alert.message;
    payload["timestamp_ms"] = alert.timestamp_ms;
    if (alert.chain_id.has_value())
        payload["chain_id"] = *alert.chain_id;
    if (alert.token_address.has_value())
        payload["token_address"] = *alert.token_address;
    if (alert.amount_decimal.has_value())
        payload["amount_decimal"] = *alert.amount_decimal;

    const std::string body = payload.dump();

    // Capture send timestamp once per dispatch (replay-protection window
    // is the same for all endpoints of this alert).
    const std::string timestamp_ms_str = std::to_string(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    for (const auto &endpoint : it->second) {
        std::string signature;
        if (!endpoint.hmac_secret.empty()) {
            // Sign "<timestamp_ms>.<body>" so the timestamp header is
            // authenticated and cannot be swapped on a replayed request.
            // This matches the Stripe / GitHub webhook signing pattern.
            const std::string signing_string = timestamp_ms_str + "." + body;
            signature = "sha256=" +
                        sentinel::security::hmac_sha256_hex(
                            endpoint.hmac_secret, signing_string);
        }

        CURL *curl = curl_easy_init();
        if (!curl) {
            Lalert.error(
                "Webhook send failed: could not initialize curl for url={}",
                endpoint.url);
            continue;
        }

        std::string response_body;

        curl_easy_setopt(curl, CURLOPT_URL, endpoint.url.c_str());
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE,
                         static_cast<long>(body.size()));
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, string_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/json");

        const std::string ts_header =
            "X-Risk-Sentinel-Timestamp: " + timestamp_ms_str;
        headers = curl_slist_append(headers, ts_header.c_str());

        std::string sig_header;
        if (!signature.empty()) {
            sig_header = "X-Risk-Sentinel-Signature: " + signature;
            headers = curl_slist_append(headers, sig_header.c_str());
        }

        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        CURLcode res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            Lalert.error("Webhook send failed (curl): {} for url={}",
                         curl_easy_strerror(res), endpoint.url);
        } else {
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code < 200 || http_code >= 300) {
                Lalert.error(
                    "Webhook send failed: url={}, HTTP {}, body={}",
                    endpoint.url, http_code, response_body);
            } else {
                Lalert.debug("Webhook send succeeded: url={}, HTTP {}",
                             endpoint.url, http_code);
            }
        }

        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
    }
}

} // namespace sentinel::risk
