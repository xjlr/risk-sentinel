#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "sentinel/risk/alert_channel.hpp"

namespace sentinel::risk {

struct WebhookEndpoint {
    std::string url;
    std::string hmac_secret; // empty = skip signing (not recommended)
};

class WebhookAlertChannel : public IAlertChannel {
public:
    explicit WebhookAlertChannel(
        std::unordered_map<std::uint64_t, std::vector<WebhookEndpoint>>
            customer_webhooks);
    ~WebhookAlertChannel() override;

    void send(const Alert &alert) override;
    std::string name() const override { return "webhook"; }

private:
    std::unordered_map<std::uint64_t, std::vector<WebhookEndpoint>>
        customer_webhooks_;
};

} // namespace sentinel::risk
