#pragma once
#include "SimConfig.hpp"
#include "engine/ChunkMetric.hpp"
#include "fut/FutInterface.hpp"
#include <vector>

// =============================================================================
// SimEngine  —  Tier 1 + Tier 2
//
// Drives the main simulation loop. Behaviour is controlled by SimConfig:
//
//   SEQUENTIAL mode (Tier 1 behaviour):
//     FUT always runs to completion. Output is always FUT's result.
//     Overruns are detected and logged but do not affect output.
//
//   REALTIME mode (Tier 2):
//     A VirtualClock advances by one period per chunk regardless of FUT time.
//     If FUT exceeded the deadline, output is replaced by XrunPolicy.
//     Cumulative debt is tracked and reported.
//
//   Both modes:
//     - Wall clock measured via CLOCK_MONOTONIC
//     - CPU clock measured via CLOCK_THREAD_CPUTIME_ID (if probe_cpu_time=true)
//     - Context switches probed via /proc/self/status (if probe_context_switches=true)
//     - Page faults probed via getrusage(RUSAGE_THREAD) (if probe_page_faults=true)
//     - Pre-fill policy applied to output buffer before each FUT call
//     - Per-chunk CSV log written to cfg.log_file
//     - Summary printed to stdout after loop
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
    // Timing
    double wall_now_us() const;   // CLOCK_MONOTONIC in µs

    // Startup checks
    void check_cpu_governor()  const;
    void try_rt_scheduling()   const;

    // Per-chunk logging
    void print_chunk_log(const ChunkMetric& m) const;

    SimConfig                m_config;
    FutFn                    m_fut;
    std::vector<ChunkMetric> m_metrics;
};

