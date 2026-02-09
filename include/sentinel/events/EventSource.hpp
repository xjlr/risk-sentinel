#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rigtorp/SPSCQueue.h>

#include "sentinel/events/RawLog.hpp"
#include "sentinel/events/NormalizedEvent.hpp"
#include "sentinel/events/normalize.hpp"

// Forward declare – adapter interfész
class ChainAdapter;

namespace sentinel::events {

struct EventSourceConfig {
    uint64_t start_block;                 // checkpoint
    uint64_t max_block_range = 1000;       // eth_getLogs chunk
    std::chrono::milliseconds poll_interval{500};
};

class EventSource {
public:
    EventSource(
        ChainAdapter& adapter,
        rigtorp::SPSCQueue<NormalizedEvent>& out_queue,
        EventSourceConfig cfg
    );

    // Thread entry point
    void run();

    // Clean shutdown
    void stop();

private:
    bool poll_once();

private:
    ChainAdapter& adapter_;
    rigtorp::SPSCQueue<NormalizedEvent>& out_;
    uint64_t chain_id_;

    EventSourceConfig cfg_;

    std::atomic<bool> running_{true};
    uint64_t next_block_;
};

} // namespace sentinel::events
