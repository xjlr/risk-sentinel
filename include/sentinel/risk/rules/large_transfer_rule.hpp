#pragma once

#include "sentinel/events/utils/hex.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"
#include "sentinel/risk/rule_interface.hpp"

#include <array>
#include <cstring>

namespace sentinel::risk {

class LargeTransferRule : public IRiskRule {
public:
  LargeTransferRule(std::array<uint8_t, 20> token_address,
                    std::array<uint8_t, 32> threshold_be)
      : token_address_(token_address), threshold_be_(threshold_be) {}

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
      out.push_back(Alert{.message = "Large transfer detected",
                          .timestamp_ms = signal.meta.timestamp_ms});
    }
  }

private:
  std::array<uint8_t, 20> token_address_;
  std::array<uint8_t, 32> threshold_be_;
};

} // namespace sentinel::risk