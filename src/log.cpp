#include "sentinel/log.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace sentinel {

static const char* component_name(LogComponent c) {
    switch (c) {
        case LogComponent::Core:    return "core";
        case LogComponent::Rpc:     return "rpc";
        case LogComponent::Poller:  return "poller";
        case LogComponent::Adapter: return "adapter";
        case LogComponent::Db:      return "db";
        case LogComponent::Alert:   return "alert";
        default:                    return "unknown";
    }
}

static std::once_flag g_init_flag;
static std::array<std::shared_ptr<spdlog::logger>,
                  static_cast<size_t>(LogComponent::_Count)> g_loggers;

void init_logging(bool debug) {
    std::call_once(g_init_flag, [debug]() {
        auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();

        // Pattern: [time] [level] [component] message
        sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%n] %v");

        auto level = debug ? spdlog::level::debug : spdlog::level::info;

        for (int i = 0; i < static_cast<int>(LogComponent::_Count); ++i) {
            auto c = static_cast<LogComponent>(i);
            auto lg = std::make_shared<spdlog::logger>(component_name(c), sink);

            lg->set_level(level);
            lg->flush_on(spdlog::level::err); // optional

            g_loggers[static_cast<size_t>(i)] = std::move(lg);
        }
    });
}

spdlog::logger& logger(LogComponent c) {
    // Safe default: if user forgot to call init, init with info-level
    init_logging(false);
    return *g_loggers[static_cast<size_t>(c)];
}

} // namespace sentinel
