#pragma once

#include "sentinel/events/RawLog.hpp"
#include "sentinel/events/NormalizedEvent.hpp"

#include <cstdint>

namespace sentinel::events {

// EventSource tudja a chain_id-t, és (ha akarod) a block_timestamp-et is.
void normalize(const RawLog& raw,
               NormalizedEvent& out,
               uint64_t chain_id,
               uint64_t block_timestamp /*=0*/);

} // namespace sentinel::events
