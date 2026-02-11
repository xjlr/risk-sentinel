#include "sentinel/events/EventSource.hpp"

#include <thread>
#include <vector>

#include "sentinel/chains/ChainAdapter.hpp"
#include "sentinel/log.hpp"

namespace sentinel::events {

EventSource::EventSource(
    ChainAdapter& adapter,
    rigtorp::SPSCQueue<NormalizedEvent>& out_queue,
    EventSourceConfig cfg
)
    : adapter_(adapter)
    , out_(out_queue)
    , cfg_(cfg)
    , next_block_(cfg.start_block)
    , cold_start_(cfg_.start_block == 0)
    , log_(sentinel::logger(sentinel::LogComponent::EventSource))
{
    chain_id_ = adapter_.chainId();
}

void EventSource::stop() {
    running_.store(false, std::memory_order_relaxed);
}

void EventSource::run() {
    log_.info("EventSource started (chain_id={}, start_block={})", chain_id_, next_block_);

    while (running_) {
        bool work_done = false;
        try {
            work_done = poll_once();
        } catch (const std::exception& e) {
            log_.error("poll_once failed: {}", e.what());
            std::this_thread::sleep_for(cfg_.idle_sleep);
            continue;
        }

        if (work_done) {
            std::this_thread::yield();
        } else {
            std::this_thread::sleep_for(cfg_.idle_sleep);
        }
    }

    log_.info("EventSource stopped");
}


void EventSource::push_blocking(const NormalizedEvent& ev) {
    int retries = 0;
    while (!out_.try_push(ev)) {
        if ((retries++ % 1000) == 0) {
            log_.warn("RingBuffer full: blocking producer (retries={})", retries);
        }
        std::this_thread::sleep_for(cfg_.push_backoff);
    }
}

bool EventSource::poll_once() {
    // return: true = there is work to do (catch-up), false = reached the head (idle)

    // 1) Refresh chain head if needed (not initialized yet, or cursor passed old head)
    if (cached_chain_head_ == 0 || next_block_ > cached_chain_head_) {
        try {
            cached_chain_head_ = adapter_.latestBlock();
            log_.debug("Chain head updated: {}", cached_chain_head_);
        } catch (const std::exception& e) {
            throw std::runtime_error(std::string("latestBlock failed: ") + e.what());
        }

        // Cold start behavior: if there is no checkpoint, jump to head once.
        // (Optionally use a lookback here instead of exact head.)
        if (cold_start_) {
            next_block_ = cached_chain_head_;
            cold_start_ = false;

            log_.info("Cold start: jumping to chain head {}", next_block_);
            return false; // now we're caught up; let the run() loop sleep (idle mode)
        }
    }

    // 1b) If still no work (already caught up), sleep in caller
    if (next_block_ > cached_chain_head_) {
        return false; // idle
    }

    // 2) Choose batch size based on distance to head, capped by max_block_range
    const uint64_t distance = cached_chain_head_ - next_block_ + 1;
    uint64_t range = std::min<uint64_t>(cfg_.max_block_range, distance);
    range = std::max<uint64_t>(range, cfg_.min_block_range);

    // 3) Fetch logs with range-shrink retry on errors
    std::vector<RawLog> logs;
    uint64_t to_block = next_block_ + range - 1;

    while (true) {
        to_block = next_block_ + range - 1;

        log_.debug("Polling blocks [{}..{}] (head={}, distance={}, range={})",
                   next_block_, to_block, cached_chain_head_, distance, range);

        try {
            logs = adapter_.getLogs(next_block_, to_block);
            break;
        } catch (const std::exception& e) {
            log_.warn("getLogs error: {}", e.what());

            if (range <= cfg_.min_block_range) {
                throw std::runtime_error("Persistent RPC failure even with min_range");
            }

            range = std::max<uint64_t>(range / 2, cfg_.min_block_range);
        }
    }

    // 4) Normalize + push (with backpressure)
    for (const RawLog& raw : logs) {
        NormalizedEvent ev{};
        normalize(raw, ev, chain_id_, /*timestamp=*/0);
        push_blocking(ev);
    }

    // 5) Advance cursor (always based on the requested block range, not on logs)
    next_block_ = to_block + 1;

    // 6) Tell run() whether it should continue immediately (still behind head)
    return (next_block_ <= cached_chain_head_);
}


} // namespace sentinel::events
