#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace sentinel::health {

// Thread-safe heartbeat tracker. Each pipeline thread owns one of these
// and calls record() at least once per main-loop iteration (including
// idle iterations). The health server reads via last_ms().
class Heartbeat {
public:
    void record() noexcept {
        last_ms_.store(now_ms_(), std::memory_order_relaxed);
    }

    uint64_t last_ms() const noexcept {
        return last_ms_.load(std::memory_order_relaxed);
    }

    uint64_t age_ms(uint64_t now_ms) const noexcept {
        const uint64_t last = last_ms();
        if (last == 0) return UINT64_MAX;   // never recorded
        if (now_ms < last) return 0;        // clock skew: treat as fresh
        return now_ms - last;
    }

private:
    // Uses steady_clock so heartbeats are immune to NTP/DST wall-clock jumps.
    static uint64_t now_ms_() noexcept {
        return static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    std::atomic<uint64_t> last_ms_{0};
};

} // namespace sentinel::health
