#pragma once
#include <string>
#include <cstdint>

class ChainAdapter {
public:
    virtual ~ChainAdapter() = default;

    virtual std::string name() const = 0;
    virtual uint64_t chainId() = 0;
    virtual uint64_t latestBlock() = 0;
};
