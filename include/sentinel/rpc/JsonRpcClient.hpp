#pragma once

#include <nlohmann/json.hpp>
#include <string>

#include <mutex>
#include <unordered_map>

namespace sentinel::metrics {
struct Metrics;
}

namespace prometheus {
class Counter;
class Gauge;
class Histogram;
}

class JsonRpcClient {
public:
    explicit JsonRpcClient(std::string endpoint, std::string chain_name, sentinel::metrics::Metrics* metrics = nullptr);

    nlohmann::json call(
        const std::string& method,
        const nlohmann::json& params = nlohmann::json::array()
    );

private:
    std::string endpoint_;
    std::string chain_name_;
    sentinel::metrics::Metrics* metrics_;

    prometheus::Counter* get_rpc_counter(const std::string& method, const std::string& status);
    prometheus::Histogram* get_rpc_histogram(const std::string& method);

    std::mutex metrics_mutex_;
    prometheus::Gauge* last_rpc_success_gauge_ = nullptr;
    std::unordered_map<std::string, prometheus::Counter*> rpc_counters_;
    std::unordered_map<std::string, prometheus::Histogram*> rpc_histograms_;
};
