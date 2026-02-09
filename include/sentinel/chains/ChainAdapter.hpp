#pragma once
#include <string>
#include <cstdint>

#include "sentinel/events/RawLog.hpp"

class ChainAdapter {
public:
    virtual ~ChainAdapter() = default;

    virtual std::string name() const = 0;
    virtual uint64_t chainId() = 0;
    virtual uint64_t latestBlock() = 0;

    virtual std::vector<sentinel::events::RawLog>
    getLogs(uint64_t from_block, uint64_t to_block) = 0;
};
