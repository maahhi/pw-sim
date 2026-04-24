#include "engine/ProbeReader.hpp"
#include <cstdio>
#include <cstring>

// =============================================================================
// ProbeSnapshot  —  read /proc/self/status for context switch counters
//
// /proc/self/status contains lines like:
//   voluntary_ctxt_switches:   142
//   nonvoluntary_ctxt_switches: 3
//
// These are cumulative per-process counters. We read them before and after
// the FUT call and compute the delta. Reading the file takes ~1–3 µs.
// =============================================================================

static void read_ctx_switches(long& vol, long& invol) {
    vol   = 0;
    invol = 0;

    FILE* f = std::fopen("/proc/self/status", "r");
    if (!f) return;

    char line[256];
    int  found = 0;
    while (found < 2 && std::fgets(line, sizeof(line), f)) {
        if (std::sscanf(line, "voluntary_ctxt_switches: %ld", &vol)   == 1) ++found;
        if (std::sscanf(line, "nonvoluntary_ctxt_switches: %ld", &invol) == 1) ++found;
    }
    std::fclose(f);
}

// =============================================================================
// ProbeReader implementation
// =============================================================================

ProbeSnapshot ProbeReader::snapshot() {
    ProbeSnapshot s;

    // Context switches from /proc/self/status
    read_ctx_switches(s.vol_ctx, s.invol_ctx);

    // Page faults from getrusage(RUSAGE_THREAD)
    // RUSAGE_THREAD measures the calling thread only — exactly what we want.
    struct rusage ru;
    if (getrusage(RUSAGE_THREAD, &ru) == 0) {
        s.min_faults = ru.ru_minflt;
        s.maj_faults = ru.ru_majflt;
    }

    return s;
}

ProbeDelta ProbeReader::delta(const ProbeSnapshot& before,
                              const ProbeSnapshot& after)
{
    ProbeDelta d;
    d.voluntary_ctx_switches   = after.vol_ctx    - before.vol_ctx;
    d.involuntary_ctx_switches = after.invol_ctx  - before.invol_ctx;
    d.page_faults_minor        = after.min_faults - before.min_faults;
    d.page_faults_major        = after.maj_faults - before.maj_faults;
    return d;
}

double ProbeReader::cpu_now_us() {
    struct timespec ts;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0.0;
    return static_cast<double>(ts.tv_sec)  * 1e6 +
           static_cast<double>(ts.tv_nsec) / 1e3;
}
