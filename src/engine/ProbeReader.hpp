#pragma once
#include <cstddef>
#include <ctime>       // clock_gettime
#include <sys/resource.h>  // getrusage

// =============================================================================
// ProbeReader
//
// Lightweight probes that snapshot OS-level per-thread counters before and
// after each FUT call. The delta reveals RT-hostile behaviour inside FUT.
//
// All reads happen on the calling thread with no syscalls heavier than
// reading /proc/self/status and getrusage — total overhead < 5 µs per chunk.
//
// What each probe catches:
//
//   voluntary_ctx_switches   > 0
//     FUT called something that voluntarily yielded the CPU:
//     sleep(), blocking IO, mutex wait, condition variable wait.
//     In a real PipeWire plugin this would cause an immediate xrun.
//
//   involuntary_ctx_switches > 0
//     The OS preempted FUT mid-execution to run another process.
//     On a real RT system with SCHED_FIFO this cannot happen.
//     In the simulator it inflates wall_us — explains why p99 >> p50.
//
//   page_faults_minor > 0
//     FUT accessed memory that wasn't in the TLB (page table walk needed).
//     Common cause: first inference call of an ML model, heap allocation,
//     or touching a large weight tensor for the first time.
//     Does not require disk IO but still enters the kernel.
//
//   page_faults_major > 0
//     FUT caused a page fault that required disk IO (swapped-out page).
//     Extremely bad in RT — can take milliseconds. Should never happen
//     in a properly locked/pinned audio process.
// =============================================================================

struct ProbeSnapshot {
    long vol_ctx   = 0;   // voluntary context switches (from /proc/self/status)
    long invol_ctx = 0;   // involuntary context switches
    long min_faults = 0;  // minor page faults (from getrusage RUSAGE_THREAD)
    long maj_faults = 0;  // major page faults
};

struct ProbeDelta {
    long voluntary_ctx_switches   = 0;
    long involuntary_ctx_switches = 0;
    long page_faults_minor        = 0;
    long page_faults_major        = 0;
};

class ProbeReader {
public:
    // Take a snapshot of all counters right now.
    static ProbeSnapshot snapshot();

    // Compute delta between two snapshots (after - before).
    static ProbeDelta    delta(const ProbeSnapshot& before,
                               const ProbeSnapshot& after);

    // CPU clock: read CLOCK_THREAD_CPUTIME_ID in microseconds.
    // Returns 0.0 if the clock is unavailable on this platform.
    static double cpu_now_us();
};
