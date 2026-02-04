#pragma once
#include <string>
#include <nlohmann/json.hpp>

class JsonRpcClient {
public:
    explicit JsonRpcClient(std::string endpoint);

    nlohmann::json call(
        const std::string& method,
        const nlohmann::json& params = nlohmann::json::array()
    );

private:
    std::string endpoint_;
};
