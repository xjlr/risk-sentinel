#pragma once

#include "sentinel/risk/rule_interface.hpp"
#include "sentinel/risk/alert_dispatcher.hpp"

namespace sentinel::risk {

// A minimal example rule implementation as requested
class ExampleLargeSwapRule : public IRiskRule {
public:
    SignalMask interests() const override {
        // Only interested in Swap events
        return make_mask(SignalType::Swap);
    }

    void evaluate(
        const Signal& signal,
        StateStore& /* state_store */,
        std::vector<Alert>& out
    ) override {
        // Safe to assume type == Swap because RiskEngine routing guarantees this
        if (signal.type == SignalType::Swap) {
            
            // Example logic (would normally inspect signal.payload)
            // No network I/O, no dynamically allocated objects in hot path.
            
            out.push_back(Alert{
                .message = "Large swap detected by Example Rule!",
                .timestamp_ms = signal.meta.timestamp_ms
            });
        }
    }
};

} // namespace sentinel::risk
