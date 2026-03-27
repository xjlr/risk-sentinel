#pragma once

#include <nlohmann/json.hpp>
#include <string>

namespace sentinel::metrics {
struct Metrics;
}

class JsonRpcClient {
public:
    explicit JsonRpcClient(std::string endpoint, sentinel::metrics::Metrics* metrics = nullptr);

    nlohmann::json call(
        const std::string& method,
        const nlohmann::json& params = nlohmann::json::array()
    );

private:
    std::string endpoint_;
    sentinel::metrics::Metrics* metrics_;
};
