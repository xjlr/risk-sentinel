#pragma once
#include <spdlog/spdlog.h>

namespace sentinel {

enum class LogComponent : int {
    Core = 0,
    Rpc,
    EventSource,
    Adapter,
    Db,
    Alert,
    _Count
};

void init_logging(bool debug);

// Fast O(1) access, safe after init_logging()
spdlog::logger& logger(LogComponent c);

} // namespace sentinel

// Convenience macros (optional)
#define LOG_CORE_INFO(...)  ::sentinel::logger(::sentinel::LogComponent::Core).info(__VA_ARGS__)
#define LOG_RPC_DEBUG(...)  ::sentinel::logger(::sentinel::LogComponent::Rpc).debug(__VA_ARGS__)
#define LOG_RPC_ERROR(...)  ::sentinel::logger(::sentinel::LogComponent::Rpc).error(__VA_ARGS__)
