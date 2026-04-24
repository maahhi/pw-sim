#pragma once
#include <cstddef>
#include <cstdint>

// =============================================================================
// ChunkMetric
//
// Plain data struct — one instance per chunk processed.
// Collected by SimEngine, written to CSV by MetricsWriter.
// =============================================================================

struct ChunkMetric {

    // ── Identity ──────────────────────────────────────────────────────────────
    size_t chunk_index   = 0;      // 0-based counter, excludes warmup chunks
    size_t frames        = 0;      // actual frames in this chunk (< chunk_size on last chunk)
    bool   is_warmup     = false;

    // ── Wall clock (CLOCK_MONOTONIC) ──────────────────────────────────────────
    double wall_us       = 0.0;    // wall-clock duration of FUT call in µs
    double deadline_us   = 0.0;    // effective budget for this chunk in µs
    double wall_ratio    = 0.0;    // wall_us / deadline_us  (> 1.0 = overrun)
    bool   overrun       = false;  // true if wall_us > deadline_us

    // ── CPU clock (CLOCK_THREAD_CPUTIME_ID) ───────────────────────────────────
    // Measures only CPU time on the calling thread.
    // Will undercount if FUT spawns worker threads internally.
    // cpu_us < wall_us → FUT spent time waiting (sleep, IO, mutex).
    // cpu_us ≈ wall_us → FUT was compute-bound.
    double cpu_us        = 0.0;    // 0 if probe_cpu_time = false

    // ── REALTIME mode ─────────────────────────────────────────────────────────
    bool   xrun_applied      = false;  // true if xrun policy replaced output
    double cumulative_debt_us = 0.0;   // how far behind the virtual clock (µs)

    // ── RT-hostile behaviour probes ───────────────────────────────────────────
    // Read from /proc/self/status before and after FUT call.
    long   voluntary_ctx_switches   = 0;  // FUT blocked voluntarily (IO, mutex, sleep)
    long   involuntary_ctx_switches = 0;  // OS preempted FUT (would not happen with SCHED_FIFO)

    // Read from getrusage(RUSAGE_THREAD).
    long   page_faults_minor = 0;   // page reclaim (no disk IO, but still kernel entry)
    long   page_faults_major = 0;   // page fault requiring disk IO — very bad in RT
};

