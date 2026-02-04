#pragma once
#include <chrono>

class BlockPoller {
public:
    BlockPoller(
        ChainAdapter& adapter,
        CheckpointStore& checkpoints,
        std::chrono::seconds interval
    );

    void run();

private:
    ChainAdapter& adapter_;
    CheckpointStore& checkpoints_;
    std::chrono::seconds interval_;
};
