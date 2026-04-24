#include "sentinel/health/health_server.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <stdexcept>
#include <string>
#include <utility>

namespace sentinel::health {

namespace {

std::pair<std::string, int> parse_address(const std::string& addr) {
    auto pos = addr.rfind(':');
    if (pos == std::string::npos || pos == 0) {
        throw std::runtime_error("HealthServer: invalid listen_address: " + addr);
    }
    std::string host = addr.substr(0, pos);
    int port = std::stoi(addr.substr(pos + 1));
    return {std::move(host), port};
}

} // namespace

HealthServer::HealthServer(HealthServerConfig cfg, HealthCheckInputs inputs)
    : cfg_(std::move(cfg))
    , inputs_(std::move(inputs))
    , server_(std::make_unique<httplib::Server>()) {}

HealthServer::~HealthServer() {
    stop();
}

void HealthServer::start() {
    auto [host, port] = parse_address(cfg_.listen_address);

    server_->Get("/healthz", [this](const httplib::Request&, httplib::Response& res) {
        CheckResult r = evaluate_liveness();
        if (r.ok) {
            res.status = 200;
            res.set_content("OK", "text/plain");
        } else {
            res.status = 503;
            res.set_content(r.detail, "text/plain");
        }
    });

    server_->Get("/readyz", [this](const httplib::Request&, httplib::Response& res) {
        ReadinessReport report = evaluate_readiness(inputs_, cfg_.checks);

        nlohmann::json payload;
        payload["ready"] = report.ready;
        payload["checks"] = nlohmann::json::object();
        for (const auto& [name, result] : report.checks) {
            payload["checks"][name] = {{"ok", result.ok}, {"detail", result.detail}};
        }

        res.status = report.ready ? 200 : 503;
        res.set_content(payload.dump(), "application/json");
    });

    server_thread_ = std::thread([this, h = std::move(host), p = port]() {
        if (!server_->listen(h, p)) {
            spdlog::error("HealthServer: failed to listen on {}:{} — "
                          "health endpoints unavailable", h, p);
        }
    });
}

void HealthServer::stop() {
    if (server_) {
        server_->stop();
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
}

} // namespace sentinel::health
