#include "sentinel/rpc/JsonRpcClient.hpp"

#include <curl/curl.h>
#include <stdexcept>
#include <string>
#include <sstream>

namespace {

// libcurl write callback
size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

} // namespace

JsonRpcClient::JsonRpcClient(std::string endpoint)
    : endpoint_(std::move(endpoint)) {
    if (endpoint_.empty()) {
        throw std::runtime_error("JsonRpcClient endpoint is empty");
    }

    // Global init is safe to call multiple times, but we do it once here
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

nlohmann::json JsonRpcClient::call(
    const std::string& method,
    const nlohmann::json& params
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
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
        std::ostringstream oss;
        oss << "curl_easy_perform failed: " << curl_easy_strerror(rc);
        throw std::runtime_error(oss.str());
    }

    if (http_code != 200) {
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
        throw std::runtime_error(
            "JSON-RPC error: " + json["error"].dump()
        );
    }

    if (!json.contains("result")) {
        throw std::runtime_error(
            "JSON-RPC response missing result field: " + json.dump()
        );
    }

    return json;
}
