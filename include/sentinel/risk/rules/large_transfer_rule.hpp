#pragma once

#include "sentinel/events/utils/hex.hpp"
#include "sentinel/log.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/risk/rule_interface.hpp"

#include <array>
#include <cstring>
#include <string>

namespace sentinel::risk {

struct LargeTransferRuleConfig {
  uint64_t customer_id;
  uint64_t chain_id;
  std::array<uint8_t, 20> token_address;
  std::array<uint8_t, 32> threshold_be;
};

class LargeTransferRule : public IRiskRule {
public:
  LargeTransferRule(std::vector<LargeTransferRuleConfig> configs)
      : configs_(std::move(configs)),
        log_(sentinel::logger(sentinel::LogComponent::Risk)) {}

  SignalMask interests() const override {
    return make_mask(SignalType::Transfer);
  }

  std::string_view rule_type_name() const override { return "large_transfer"; }

  void evaluate(const Signal &signal, StateStore & /* state_store */,
                std::vector<Alert> &out) override {
    const auto *evm = std::get_if<EvmLogEvent>(&signal.payload);
    if (!evm) {
      return;
    }

    if (evm->removed || evm->topic_count < 3 || evm->data_size < 32 ||
        evm->truncated) {
      return;
    }

    // Match based on configs
    for (const auto &config : configs_) {
      if (evm->chain_id != config.chain_id) {
        continue;
      }

      if (evm->address != config.token_address) {
        continue;
      }

      if (sentinel::events::utils::greater_be_256(evm->data.data(),
                                                  config.threshold_be.data())) {
        if (log_.should_log(spdlog::level::debug)) {
          std::string amount_dec =
              sentinel::events::utils::uint256_be_to_decimal(evm->data.data());
          log_.debug("[{}] Large transfer detected: amount={}, timestamp_ms={}",
                     config.customer_id, amount_dec, signal.meta.timestamp_ms);
        }

        std::string amount_dec =
            sentinel::events::utils::uint256_be_to_decimal(evm->data.data());

        // Format token address back to 0x string
        std::string token_addr_str = "0x";
        for (uint8_t b : config.token_address) {
          char buf[3];
          std::snprintf(buf, sizeof(buf), "%02x", b);
          token_addr_str += buf;
        }

        out.push_back(Alert{.customer_id = config.customer_id,
                            .rule_type = "large_transfer",
                            .message = "Large transfer detected",
                            .timestamp_ms = signal.meta.timestamp_ms,
                            .amount_decimal = amount_dec,
                            .token_address = token_addr_str,
                            .chain_id = config.chain_id});
      }
    }
  }

private:
  std::vector<LargeTransferRuleConfig> configs_;
  spdlog::logger &log_;
};

} // namespace sentinel::risk