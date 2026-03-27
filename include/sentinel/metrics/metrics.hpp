#pragma once

#include <memory>
#include <string>

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>

namespace sentinel::metrics {

struct Metrics {
    // Exposer
    std::unique_ptr<prometheus::Exposer> exposer;
    std::shared_ptr<prometheus::Registry> registry;

    // Counters
    prometheus::Counter& events_ingested_total;
    prometheus::Counter& signals_normalized_total;
    prometheus::Counter& alerts_generated_total;
    prometheus::Counter& alerts_sent_total;
    prometheus::Counter& alerts_send_failures_total;
    prometheus::Counter& rpc_errors_total;

    // Gauges
    prometheus::Gauge& ring_buffer_depth;
    prometheus::Gauge& alert_queue_depth;
    prometheus::Gauge& last_rpc_success_timestamp_seconds;
    prometheus::Gauge& last_alert_success_timestamp_seconds;

    // Histograms
    prometheus::Histogram& alert_send_duration_seconds;
    prometheus::Histogram& signal_to_alert_seconds;

    explicit Metrics(const std::string& listen_address);
    ~Metrics() = default;
};

} // namespace sentinel::metrics
