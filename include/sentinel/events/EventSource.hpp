#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include <rigtorp/SPSCQueue.h>

#include "sentinel/events/RawLog.hpp"
#include "sentinel/events/NormalizedEvent.hpp"
#include "sentinel/events/normalize.hpp"
#include "sentinel/log.hpp"

// Forward declare
class ChainAdapter;

namespace sentinel::events {

struct EventSourceConfig {
    uint64_t start_block = 0;
    uint64_t max_block_range = 1000;

    std::chrono::milliseconds idle_sleep{200};     // live mode
    std::chrono::milliseconds error_backoff{1000}; // after an error
    std::chrono::microseconds push_backoff{10};    // queue is full
    uint64_t min_block_range = 1;                  // retry halfening
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
    void push_blocking(const NormalizedEvent& ev);
    bool poll_once();

private:
    ChainAdapter& adapter_;
    rigtorp::SPSCQueue<NormalizedEvent>& out_;
    uint64_t chain_id_;

    EventSourceConfig cfg_;

    std::atomic<bool> running_{true};
    uint64_t next_block_;
    bool cold_start_;
    uint64_t cached_chain_head_ = 0;

    spdlog::logger& log_;
};

} // namespace sentinel::events
