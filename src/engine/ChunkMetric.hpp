#pragma once
#include <cstddef>
#include <cstdint>

// =============================================================================
// ChunkMetric
//
// Plain data struct — one instance per chunk processed.
// Collected by SimEngine, consumed by the reporter.
// Tier 2 will add: cpu_us, xrun_applied, cumulative_debt_us,
//                  voluntary_ctx_switches, involuntary_ctx_switches, page_faults
// =============================================================================

struct ChunkMetric {
    size_t chunk_index  = 0;     // 0-based, excludes warmup
    size_t frames       = 0;     // actual frames processed (may be < chunk_size on last chunk)

    double wall_us      = 0.0;   // wall-clock duration of FUT call in microseconds
    double deadline_us  = 0.0;   // budget for this chunk in microseconds
    double wall_ratio   = 0.0;   // wall_us / deadline_us  (>1.0 = overrun)

    bool   overrun      = false; // true if wall_us > deadline_us
    bool   is_warmup    = false; // true if this chunk was a warmup chunk
};
