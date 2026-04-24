#pragma once

#include <memory>
#include <string>

#include <prometheus/counter.h>
#include <prometheus/exposer.h>
#include <prometheus/family.h>
#include <prometheus/gauge.h>
#include <prometheus/histogram.h>
#include <prometheus/registry.h>

namespace sentinel::metrics {

struct Metrics {
    // Exposer
    std::unique_ptr<prometheus::Exposer> exposer;
    std::shared_ptr<prometheus::Registry> registry;

    // Counters
    prometheus::Family<prometheus::Counter>& events_ingested_total;
    prometheus::Family<prometheus::Counter>& signals_normalized_total;
    prometheus::Family<prometheus::Counter>& alerts_generated_total;
    prometheus::Family<prometheus::Counter>& alerts_sent_total;
    prometheus::Family<prometheus::Counter>& alerts_send_failures_total;
    prometheus::Family<prometheus::Counter>& alerts_deduplicated_total;
    prometheus::Family<prometheus::Counter>& rpc_calls_total;

    // Gauges
    prometheus::Family<prometheus::Gauge>& ring_buffer_depth;
    prometheus::Family<prometheus::Gauge>& alert_queue_depth;
    prometheus::Family<prometheus::Gauge>& last_rpc_success_timestamp_seconds;
    prometheus::Family<prometheus::Gauge>& last_alert_success_timestamp_seconds;
    prometheus::Family<prometheus::Gauge>& last_seen_block;
    prometheus::Family<prometheus::Gauge>& last_processed_block;

    // Histograms
    prometheus::Family<prometheus::Histogram>& alert_send_duration_seconds;
    prometheus::Family<prometheus::Histogram>& signal_to_alert_seconds;
    prometheus::Family<prometheus::Histogram>& rpc_call_duration_seconds;

    // Canonical Chain Label
    std::string chain_name;

    // Single-label (chain) cached metric pointers
    prometheus::Counter* events_ingested_chain = nullptr;
    prometheus::Counter* signals_normalized_chain = nullptr;
    prometheus::Gauge* ring_buffer_depth_chain = nullptr;
    prometheus::Gauge* alert_queue_depth_chain = nullptr;
    prometheus::Gauge* last_rpc_success_timestamp_seconds_chain = nullptr;
    prometheus::Gauge* last_alert_success_timestamp_seconds_chain = nullptr;
    prometheus::Gauge* last_seen_block_chain = nullptr;
    prometheus::Gauge* last_processed_block_chain = nullptr;

    explicit Metrics(const std::string& listen_address, const std::string& chain);
    ~Metrics() = default;
};

} // namespace sentinel::metrics
