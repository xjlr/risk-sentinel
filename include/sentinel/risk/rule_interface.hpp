#pragma once

#include "signal.hpp"
#include <vector>

namespace sentinel::risk {

// Forward declarations
class StateStore;
struct Alert;

class IRiskRule {
public:
    virtual ~IRiskRule() = default;

    virtual SignalMask interests() const = 0;

    virtual void evaluate(
        const Signal& signal,
        StateStore& state_store, // Dummy StateStore for the specification compile
        std::vector<Alert>& out
    ) = 0;
};

// Dummy StateStore just for compilation to succeed
class StateStore {
public:
    StateStore() = default;
};

} // namespace sentinel::risk
