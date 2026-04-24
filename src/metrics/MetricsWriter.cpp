#include "metrics/MetricsWriter.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>
#include <numeric>   // std::accumulate

// =============================================================================
// MetricsWriter
// =============================================================================

MetricsWriter::MetricsWriter(const std::string& csv_path)
    : m_path(csv_path)
{}

MetricsWriter::~MetricsWriter() {
    close();
}

void MetricsWriter::open() {
    m_file = std::fopen(m_path.c_str(), "w");
    if (!m_file) {
        throw std::runtime_error(
            "MetricsWriter: cannot open '" + m_path + "' for writing");
    }

    // Header row
    std::fprintf(m_file,
        "chunk_idx,"
        "frames,"
        "wall_us,"
        "cpu_us,"
        "deadline_us,"
        "overrun,"
        "wall_ratio,"
        "xrun_applied,"
        "cumulative_debt_us,"
        "vol_ctx_sw,"
        "invol_ctx_sw,"
        "page_faults_minor,"
        "page_faults_major\n"
    );
}

void MetricsWriter::write_chunk(const ChunkMetric& m) {
    if (!m_file) return;
    std::fprintf(m_file,
        "%zu,%zu,%.3f,%.3f,%.3f,%d,%.6f,%d,%.3f,%ld,%ld,%ld,%ld\n",
        m.chunk_index,
        m.frames,
        m.wall_us,
        m.cpu_us,
        m.deadline_us,
        m.overrun       ? 1 : 0,
        m.wall_ratio,
        m.xrun_applied  ? 1 : 0,
        m.cumulative_debt_us,
        m.voluntary_ctx_switches,
        m.involuntary_ctx_switches,
        m.page_faults_minor,
        m.page_faults_major
    );
}

void MetricsWriter::close() {
    if (m_file) {
        std::fflush(m_file);
        std::fclose(m_file);
        m_file = nullptr;
    }
}

// =============================================================================
// print_summary — static, called by SimEngine after the loop
// =============================================================================

static double percentile(std::vector<double>& sorted, double pct) {
    if (sorted.empty()) return 0.0;
    size_t idx = static_cast<size_t>(pct / 100.0 * static_cast<double>(sorted.size()));
    if (idx >= sorted.size()) idx = sorted.size() - 1;
    return sorted[idx];
}

void MetricsWriter::print_summary(const std::vector<ChunkMetric>& metrics,
                                   const std::string& clock_mode_str)
{
    if (metrics.empty()) {
        std::printf("  No metrics collected.\n");
        return;
    }

    // ── Collect vectors for percentile computation ───────────────────────────
    std::vector<double> wall_vals, cpu_vals;
    wall_vals.reserve(metrics.size());
    cpu_vals.reserve(metrics.size());

    size_t overrun_count     = 0;
    size_t xrun_count        = 0;
    size_t vol_ctx_chunks    = 0;
    size_t invol_ctx_chunks  = 0;
    size_t minor_fault_chunks = 0;
    size_t major_fault_chunks = 0;
    double max_debt_us       = 0.0;
    double total_glitch_us   = 0.0;

    for (const auto& m : metrics) {
        wall_vals.push_back(m.wall_us);
        cpu_vals.push_back(m.cpu_us);
        if (m.overrun)                          ++overrun_count;
        if (m.xrun_applied)                     ++xrun_count;
        if (m.voluntary_ctx_switches   > 0)     ++vol_ctx_chunks;
        if (m.involuntary_ctx_switches > 0)     ++invol_ctx_chunks;
        if (m.page_faults_minor        > 0)     ++minor_fault_chunks;
        if (m.page_faults_major        > 0)     ++major_fault_chunks;
        if (m.cumulative_debt_us > max_debt_us) max_debt_us = m.cumulative_debt_us;
        if (m.xrun_applied)                     total_glitch_us += m.deadline_us;
    }

    std::vector<double> wall_sorted = wall_vals;
    std::vector<double> cpu_sorted  = cpu_vals;
    std::sort(wall_sorted.begin(), wall_sorted.end());
    std::sort(cpu_sorted.begin(),  cpu_sorted.end());

    const size_t n        = metrics.size();
    const double budget   = metrics[0].deadline_us;
    const double w_p50    = percentile(wall_sorted, 50);
    const double w_p95    = percentile(wall_sorted, 95);
    const double w_p99    = percentile(wall_sorted, 99);
    const double w_max    = wall_sorted.back();
    const double c_p50    = percentile(cpu_sorted,  50);
    const double c_p95    = percentile(cpu_sorted,  95);
    const double c_p99    = percentile(cpu_sorted,  99);
    const double c_max    = cpu_sorted.back();
    const double overrun_pct = 100.0 * static_cast<double>(overrun_count)
                                     / static_cast<double>(n);
    const double total_glitch_ms = total_glitch_us / 1000.0;

    // ── Verdict ──────────────────────────────────────────────────────────────
    const char* verdict;
    const char* icon;
    if (overrun_count == 0) {
        verdict = "PASS";     icon = "✅";
    } else if (overrun_pct < 1.0) {
        verdict = "MARGINAL"; icon = "⚠ ";
    } else {
        verdict = "FAIL";     icon = "❌";
    }

    // ── Print ─────────────────────────────────────────────────────────────────
    std::printf("\n");
    std::printf("╔══════════════════════════════════════════════════════╗\n");
    std::printf("║                      Summary                        ║\n");
    std::printf("╚══════════════════════════════════════════════════════╝\n");
    std::printf("  clock mode      : %s\n",   clock_mode_str.c_str());
    std::printf("  chunks measured : %zu\n",  n);
    std::printf("  deadline budget : %.1f µs\n", budget);
    std::printf("\n");

    std::printf("  wall latency (CLOCK_MONOTONIC)\n");
    std::printf("    p50  : %9.1f µs   (%.3fx budget)\n", w_p50, w_p50 / budget);
    std::printf("    p95  : %9.1f µs   (%.3fx budget)\n", w_p95, w_p95 / budget);
    std::printf("    p99  : %9.1f µs   (%.3fx budget)\n", w_p99, w_p99 / budget);
    std::printf("    max  : %9.1f µs   (%.3fx budget)\n", w_max, w_max / budget);
    std::printf("\n");

    if (c_p50 > 0.0) {
        std::printf("  cpu latency  (CLOCK_THREAD_CPUTIME_ID)\n");
        std::printf("    p50  : %9.1f µs   (%.3fx budget)\n", c_p50, c_p50 / budget);
        std::printf("    p95  : %9.1f µs   (%.3fx budget)\n", c_p95, c_p95 / budget);
        std::printf("    p99  : %9.1f µs   (%.3fx budget)\n", c_p99, c_p99 / budget);
        std::printf("    max  : %9.1f µs   (%.3fx budget)\n", c_max, c_max / budget);
        std::printf("\n");
        // Diagnosis hint for wall >> cpu
        if (w_p99 > c_p99 * 1.5) {
            std::printf("  ℹ  wall p99 is significantly higher than cpu p99.\n"
                        "     FUT is spending time waiting (blocked on IO, mutex,\n"
                        "     or being preempted by the OS). On a real RT system\n"
                        "     with SCHED_FIFO, preemption cannot happen — but IO\n"
                        "     and mutex waits still can and will cause xruns.\n\n");
        }
    }

    std::printf("  overruns        : %zu / %zu  (%.2f%%)\n",
                overrun_count, n, overrun_pct);

    if (xrun_count > 0) {
        std::printf("  xruns applied   : %zu  (output replaced by policy)\n", xrun_count);
        std::printf("  total glitch    : %.1f ms\n", total_glitch_ms);
        std::printf("  max debt        : %.1f µs\n", max_debt_us);
    }

    std::printf("\n");

    // ── RT-hostile behaviour warnings ────────────────────────────────────────
    bool any_rt_warn = (vol_ctx_chunks || invol_ctx_chunks ||
                        minor_fault_chunks || major_fault_chunks);
    if (any_rt_warn) {
        std::printf("  RT-hostile behaviour detected\n");
        if (vol_ctx_chunks > 0)
            std::printf("    voluntary ctx switches   : %zu chunks affected\n"
                        "      → FUT blocked on something (IO / mutex / sleep)\n",
                        vol_ctx_chunks);
        if (invol_ctx_chunks > 0)
            std::printf("    involuntary ctx switches : %zu chunks affected\n"
                        "      → OS preempted FUT (would not happen with SCHED_FIFO)\n",
                        invol_ctx_chunks);
        if (minor_fault_chunks > 0)
            std::printf("    minor page faults        : %zu chunks affected\n"
                        "      → FUT touched unmapped memory (allocation / first access)\n",
                        minor_fault_chunks);
        if (major_fault_chunks > 0)
            std::printf("    major page faults        : %zu chunks affected  ⚠ CRITICAL\n"
                        "      → FUT triggered disk IO — completely unacceptable in RT\n",
                        major_fault_chunks);
        std::printf("\n");
    }

    std::printf("  verdict         : %s %s\n\n", icon, verdict);
}
