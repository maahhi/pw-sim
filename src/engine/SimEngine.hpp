#pragma once
#include "SimConfig.hpp"
#include "engine/ChunkMetric.hpp"
#include "fut/FutInterface.hpp"
#include <vector>

// =============================================================================
// SimEngine  —  Tier 1 (SEQUENTIAL clock)
//
// Drives the main simulation loop:
//   for each chunk:
//     1. read chunk_size frames from AudioReader
//     2. zero-pad if last chunk is short
//     3. if warmup: call FUT, write silence to output, skip metrics
//     4. else:
//        a. record wall clock before FUT
//        b. call FUT(input, output, frames, channels, chunk_index)
//        c. record wall clock after FUT
//        d. compute wall_us, deadline_us, wall_ratio, overrun flag
//        e. write output buffer to AudioWriter
//        f. store ChunkMetric
//   5. after loop: print per-chunk log + summary to stdout
//
// The engine owns the input and output float buffers.
// It does NOT own the FUT — caller provides it.
// =============================================================================

class SimEngine {
public:
    SimEngine(const SimConfig& config, FutFn fut);

    // Run the full simulation. Blocking. Returns when input is exhausted.
    // Throws std::runtime_error on IO failure.
    void run();

    // Access collected metrics after run() completes.
    const std::vector<ChunkMetric>& metrics() const { return m_metrics; }

private:
    // Timing helpers
    double wall_now_us() const;   // CLOCK_MONOTONIC in microseconds

    // Reporting
    void print_chunk_log(const ChunkMetric& m) const;
    void print_summary()                        const;

    SimConfig              m_config;
    FutFn                  m_fut;
    std::vector<ChunkMetric> m_metrics;
};
