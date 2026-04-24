#pragma once

#include <memory>
#include <string>
#include <thread>

#include "sentinel/health/health_checks.hpp"

// Forward declaration — full definition only needed in health_server.cpp.
namespace httplib {
class Server;
}

namespace sentinel::health {

struct HealthServerConfig {
    std::string listen_address = "0.0.0.0:8081";
    HealthChecksConfig checks;
};

class HealthServer {
public:
    HealthServer(HealthServerConfig cfg, HealthCheckInputs inputs);
    ~HealthServer();

    // Prevent copy/move
    HealthServer(const HealthServer&) = delete;
    HealthServer& operator=(const HealthServer&) = delete;

    void start();  // Non-blocking; launches the server on a dedicated thread.
    void stop();   // Idempotent; blocks until the server thread exits.

private:
    HealthServerConfig cfg_;
    HealthCheckInputs inputs_;
    std::unique_ptr<httplib::Server> server_;
    std::thread server_thread_;
};

} // namespace sentinel::health
