#pragma once

#include "sentinel/events/utils/hex.hpp"
#include "sentinel/log.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/risk/rule_interface.hpp"

#include <array>
#include <cstring>

namespace sentinel::risk {

class LargeTransferRule : public IRiskRule {
public:
  LargeTransferRule(std::array<uint8_t, 20> token_address,
                    std::array<uint8_t, 32> threshold_be)
      : token_address_(token_address), threshold_be_(threshold_be),
        log_(sentinel::logger(sentinel::LogComponent::Risk)) {}

  SignalMask interests() const override {
    return make_mask(SignalType::Transfer);
  }

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

    if (evm->address != token_address_) {
      return;
    }

    if (sentinel::events::utils::greater_be_256(evm->data.data(),
                                                threshold_be_.data())) {
      if (log_.should_log(spdlog::level::debug)) {
        std::string amount_dec =
            sentinel::events::utils::uint256_be_to_decimal(evm->data.data());
        log_.debug(
            "############ Large transfer detected: amount={}, timestamp_ms={}",
            amount_dec, signal.meta.timestamp_ms);
      }
      out.push_back(Alert{.message = "Large transfer detected",
                          .timestamp_ms = signal.meta.timestamp_ms});
    }
  }

private:
  std::array<uint8_t, 20> token_address_;
  std::array<uint8_t, 32> threshold_be_;
  spdlog::logger &log_;
};

} // namespace sentinel::risk