#include "sentinel/rpc/JsonRpcClient.hpp"
#include "sentinel/metrics/metrics.hpp"

#include <curl/curl.h>
#include <stdexcept>
#include <string>
#include <sstream>

#include <chrono>

namespace {

// libcurl write callback
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

} // namespace

JsonRpcClient::JsonRpcClient(std::string endpoint, std::string chain_name, sentinel::metrics::Metrics* metrics)
    : endpoint_(std::move(endpoint)), chain_name_(std::move(chain_name)), metrics_(metrics) {
    if (endpoint_.empty()) {
        throw std::runtime_error("JsonRpcClient endpoint is empty");
    }

    if (metrics_) {
        last_rpc_success_gauge_ = metrics_->last_rpc_success_timestamp_seconds_chain;
    }

    // Global init is safe to call multiple times, but we do it once here
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

prometheus::Counter* JsonRpcClient::get_rpc_counter(const std::string& method, const std::string& status) {
    if (!metrics_) return nullptr;
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    std::string key = method + ":" + status;
    auto it = rpc_counters_.find(key);
    if (it != rpc_counters_.end()) return it->second;
    auto* counter = &metrics_->rpc_calls_total.Add({{"chain", chain_name_}, {"method", method}, {"status", status}});
    rpc_counters_[key] = counter;
    return counter;
}

prometheus::Histogram* JsonRpcClient::get_rpc_histogram(const std::string& method) {
    if (!metrics_) return nullptr;
    std::lock_guard<std::mutex> lock(metrics_mutex_);
    auto it = rpc_histograms_.find(method);
    if (it != rpc_histograms_.end()) return it->second;
    auto* hist = &metrics_->rpc_call_duration_seconds.Add({{"chain", chain_name_}, {"method", method}}, prometheus::Histogram::BucketBoundaries{0.05, 0.1, 0.25, 0.5, 1.0, 2.5, 5.0, 10.0});
    rpc_histograms_[method] = hist;
    return hist;
}

nlohmann::json JsonRpcClient::call(
    const std::string& method,
    const nlohmann::json& params
) {
    auto start_time = std::chrono::steady_clock::now();
    CURL* curl = curl_easy_init();
    if (!curl) {
        if (auto* c = get_rpc_counter(method, "error")) c->Increment();
        throw std::runtime_error("curl_easy_init failed");
    }

    // --- Build JSON-RPC request ---
    nlohmann::json req{
        {"jsonrpc", "2.0"},
        {"id", 1},
        {"method", method},
        {"params", params}
    };

    const std::string body = req.dump();

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, body.size());

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // --- Important safety defaults ---
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);          // seconds
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);    // seconds
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);           // for multithread safety

    CURLcode rc = curl_easy_perform(curl);

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
        if (auto* c = get_rpc_counter(method, "error")) c->Increment();
        std::ostringstream oss;
        oss << "curl_easy_perform failed: " << curl_easy_strerror(rc);
        throw std::runtime_error(oss.str());
    }

    if (http_code != 200) {
        if (auto* c = get_rpc_counter(method, "error")) c->Increment();
        std::ostringstream oss;
        oss << "JSON-RPC HTTP error: " << http_code
            << ", response=" << response;
        throw std::runtime_error(oss.str());
    }

    // --- Parse JSON ---
    nlohmann::json json;
    try {
        json = nlohmann::json::parse(response);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("JSON parse failed: ") + e.what() +
            ", response=" + response
        );
    }

    // --- JSON-RPC error handling ---
    if (json.contains("error")) {
        if (auto* c = get_rpc_counter(method, "error")) c->Increment();
        throw std::runtime_error(
            "JSON-RPC error: " + json["error"].dump()
        );
    }

    if (!json.contains("result")) {
        if (auto* c = get_rpc_counter(method, "error")) c->Increment();
        throw std::runtime_error(
            "JSON-RPC response missing result field: " + json.dump()
        );
    }

    if (metrics_) {
        if (last_rpc_success_gauge_) {
            last_rpc_success_gauge_->Set(
                static_cast<double>(std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count())
            );
        }
        if (auto* c = get_rpc_counter(method, "success")) c->Increment();
        if (auto* h = get_rpc_histogram(method)) {
            h->Observe(std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count());
        }
    }

    return json;
}
