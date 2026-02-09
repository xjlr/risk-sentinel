#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace sentinel::events {
struct RawLog {
    std::string address;
    std::vector<std::string> topics;
    std::string data;
    std::string blockNumber;
    std::string transactionHash;
    std::string logIndex;
    std::string transactionIndex;
    bool removed = false;

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(
        RawLog,
        address, topics, data, blockNumber,
        transactionHash, logIndex, transactionIndex, removed
    )
};
} // end namespace sentinel::events
