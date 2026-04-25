#include "engine/ProbeReader.hpp"
#include <cstdio>
#include <cstring>

// =============================================================================
// ProbeReader implementation
// =============================================================================

ProbeSnapshot ProbeReader::snapshot() {
    ProbeSnapshot s;

#ifdef __linux__
    // Linux: parse /proc/self/status for per-process context switch counters.
    {
        FILE* f = std::fopen("/proc/self/status", "r");
        if (f) {
            char line[256];
            int  found = 0;
            while (found < 2 && std::fgets(line, sizeof(line), f)) {
                if (std::sscanf(line, "voluntary_ctxt_switches: %ld",    &s.vol_ctx)   == 1) ++found;
                if (std::sscanf(line, "nonvoluntary_ctxt_switches: %ld", &s.invol_ctx) == 1) ++found;
            }
            std::fclose(f);
        }
    }

    // Linux: RUSAGE_THREAD gives thread-level page faults.
    {
        struct rusage ru;
        if (getrusage(RUSAGE_THREAD, &ru) == 0) {
            s.min_faults = ru.ru_minflt;
            s.maj_faults = ru.ru_majflt;
        }
    }
#else
    // macOS / BSD: getrusage(RUSAGE_SELF) provides process-level counters for
    // both context switches and page faults (no POSIX thread-level equivalent).
    {
        struct rusage ru;
        if (getrusage(RUSAGE_SELF, &ru) == 0) {
            s.vol_ctx    = ru.ru_nvcsw;
            s.invol_ctx  = ru.ru_nivcsw;
            s.min_faults = ru.ru_minflt;
            s.maj_faults = ru.ru_majflt;
        }
    }
#endif

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
