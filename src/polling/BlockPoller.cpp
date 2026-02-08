#include "sentinel/polling/BlockPoller.hpp"

#include <chrono>
#include <thread>

// ide jön a logger include-od, pl.
#include "sentinel/log.hpp"

BlockPoller::BlockPoller(
    ChainAdapter& adapter,
    CheckpointStore& checkpoints,
    std::chrono::seconds interval
)
    : adapter_(adapter)
    , checkpoints_(checkpoints)
    , interval_(interval) {}

void BlockPoller::run() {
    while (true) {
        const uint64_t latest = adapter_.latestBlock();
        const uint64_t lastProcessed = checkpoints_.load();

        // LOG_INFO("[POLL] latest={}, lastProcessed={}", latest, lastProcessed);

        if (latest > lastProcessed) {
            for (uint64_t b = lastProcessed + 1; b <= latest; ++b) {
                // Day 3: még nem fetch-elünk blockot/logot, csak checkpointot léptetünk
                // LOG_INFO("[{}] processing block {}", adapter_.name(), b);

                checkpoints_.save(b);

                // LOG_INFO("[POLL] checkpoint advanced -> {}", b);
            }
        }

        std::this_thread::sleep_for(interval_);
    }
}
