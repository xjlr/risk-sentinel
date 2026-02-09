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
{
    chain_id_ = adapter_.chainId();
}

void EventSource::stop() {
    running_.store(false, std::memory_order_relaxed);
}

void EventSource::run() {
    auto& log = sentinel::logger(sentinel::LogComponent::EventSource);

    log.info("EventSource started (chain_id={}, start_block={})",
             chain_id_, next_block_);

    //log.info("Before loop!!!!! running value: {}", running_.load());

    while (running_.load(std::memory_order_relaxed)) {
        //log.info("In loop!!!!!");
        try {
            const bool did_work = poll_once();

            if (!did_work) {
                std::this_thread::sleep_for(cfg_.poll_interval);
            }
        }
        catch (const std::exception& e) {
            log.error("poll error: {}", e.what());
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }

    log.info("EventSource stopped");
}

bool EventSource::poll_once() {
    auto& log = sentinel::logger(sentinel::LogComponent::EventSource);

    const uint64_t head = adapter_.latestBlock();

    if (next_block_ > head) {
        return false;
    }

    const uint64_t to_block =
        std::min(head, next_block_ + cfg_.max_block_range - 1);

    log.debug("EventSource blocks [{}..{}]", next_block_, to_block);

    std::vector<RawLog> logs =
        adapter_.getLogs(next_block_, to_block);

    for (const RawLog& raw : logs) {
        NormalizedEvent ev{};
        normalize(raw, ev, chain_id_, /*timestamp=*/0);

        // Backpressure: block thread if buffer is full
        out_.push(std::move(ev));
    }

    next_block_ = to_block + 1;

    return true;
}

} // namespace sentinel::events
