#pragma once
#include <cstddef>
#include <string>

// =============================================================================
// pw-sim  —  Configuration  (Tier 1 + Tier 2)
//
// All knobs live here. In Tier 3 this struct will be populated from a TOML
// file + CLI overrides. For now, edit the values and recompile.
// =============================================================================

// -----------------------------------------------------------------------------
// Clock mode
// SEQUENTIAL : FUT always runs to completion, output always written.
//              No virtual clock. Overruns are flagged in log only.
//              Use for: correctness testing, hearing what FUT does to audio.
//
// REALTIME   : A virtual clock advances by one period per chunk regardless
//              of how long FUT took. If FUT exceeded the deadline, the output
//              for that chunk is replaced by xrun_policy and flagged in log.
//              Cumulative debt is tracked — models deadline debt accumulation.
//              Use for: go/no-go decision before deploying to PipeWire.
// -----------------------------------------------------------------------------
enum class ClockMode { SEQUENTIAL, REALTIME };

// -----------------------------------------------------------------------------
// Xrun policy (REALTIME mode only)
// What goes into the output buffer when FUT misses its deadline.
//
// ZEROS       : silence — what PipeWire actually does on xrun. Default.
// REPEAT_LAST : copy the previous good output chunk verbatim.
//               Sounds less jarring but masks the problem.
// PASSTHROUGH : copy the input chunk (dry, unprocessed audio).
//               Simulates a plugin that did an optimistic memcpy first.
// -----------------------------------------------------------------------------
enum class XrunPolicy { ZEROS, REPEAT_LAST, PASSTHROUGH };

// -----------------------------------------------------------------------------
// Pre-fill policy
// What the output buffer contains BEFORE FUT is called each chunk.
//
// ZEROS       : realistic — PipeWire pre-zeros the output buffer.
// PASSTHROUGH : simulates a plugin that copies input→output first, then
//               overwrites with processed audio. If FUT overruns in
//               SEQUENTIAL mode the output will contain dry audio rather
//               than whatever FUT managed to write.
// -----------------------------------------------------------------------------
enum class PreFillPolicy { ZEROS, PASSTHROUGH };

struct SimConfig {

    // -------------------------------------------------------------------------
    // IO
    // -------------------------------------------------------------------------
    std::string input_file  = "input.wav";
    std::string output_file = "output.wav";
    std::string log_file    = "pw-sim.log.csv";  // per-chunk CSV log

    // -------------------------------------------------------------------------
    // Engine
    // -------------------------------------------------------------------------

    // Clock mode — see ClockMode above
    ClockMode clock_mode = ClockMode::SEQUENTIAL;

    // Number of PCM frames handed to FUT per callback.
    // Common PipeWire values: 64, 128, 256, 512, 1024
    size_t chunk_size = 480;

    // Sample rate in Hz. Must match the input file — used to compute deadline.
    size_t sample_rate = 48000;

    // Chunks to run through FUT before recording metrics or writing real output.
    // Warmup outputs are written as silence so chunk indices match log entries.
    size_t warmup_chunks = 4;

    // What the output buffer contains before FUT is called — see PreFillPolicy
    PreFillPolicy pre_fill = PreFillPolicy::ZEROS;

    // -------------------------------------------------------------------------
    // REALTIME mode
    // -------------------------------------------------------------------------

    // What to write to the output when FUT misses the deadline — see XrunPolicy
    XrunPolicy xrun_policy = XrunPolicy::ZEROS;

    // Subtract N µs from the computed deadline to simulate a tighter real-world
    // budget (driver overhead, graph scheduling jitter, etc.).
    // 0 = use the full theoretical period. Typical real-world loss: 100–500 µs.
    double deadline_offset_us = 0.0;

    // -------------------------------------------------------------------------
    // Probes (Tier 2)
    // -------------------------------------------------------------------------

    // Measure CPU time (CLOCK_THREAD_CPUTIME_ID) alongside wall time.
    bool probe_cpu_time = true;

    // Count voluntary + involuntary context switches per chunk via /proc/self/status.
    // Nonzero voluntary   → FUT blocked on something (IO, mutex, sleep).
    // Nonzero involuntary → OS preempted FUT mid-execution.
    bool probe_context_switches = true;

    // Count minor + major page faults per chunk via getrusage(RUSAGE_THREAD).
    // Nonzero → FUT touched memory it hadn't accessed before (allocation spike).
    bool probe_page_faults = true;

    // -------------------------------------------------------------------------
    // Startup checks
    // -------------------------------------------------------------------------

    // Warn at startup if CPU governor is not 'performance'.
    bool warn_cpu_governor = true;

    // Attempt to set SCHED_FIFO on the main thread at startup.
    // Requires CAP_SYS_NICE or root. Prints warning if it fails.
    bool try_rt_priority = false;
};

// Convenience: compute effective deadline in microseconds from config.
// Applies deadline_offset_us so all callers agree on the same budget.
inline double effective_deadline_us(const SimConfig& cfg) {
    double raw = (static_cast<double>(cfg.chunk_size) /
                  static_cast<double>(cfg.sample_rate)) * 1e6;
    return raw - cfg.deadline_offset_us;
}

// Legacy alias used by Tier 1 code paths (SEQUENTIAL mode).
inline double deadline_us(const SimConfig& cfg) {
    return effective_deadline_us(cfg);
}
