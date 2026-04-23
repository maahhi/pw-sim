#include "engine/SimEngine.hpp"
#include "io/AudioReader.hpp"
#include "io/AudioWriter.hpp"

#include <algorithm>    // std::sort, std::nth_element
#include <cstring>      // memset
#include <ctime>        // clock_gettime
#include <cstdio>       // printf
#include <stdexcept>
#include <vector>
#include <cmath>        // std::isnan, std::isinf

// =============================================================================
// Internal helpers
// =============================================================================

static double timespec_to_us(const struct timespec& ts) {
    return static_cast<double>(ts.tv_sec) * 1e6 +
           static_cast<double>(ts.tv_nsec) / 1e3;
}

// =============================================================================
// SimEngine
// =============================================================================

SimEngine::SimEngine(const SimConfig& config, FutFn fut)
    : m_config(config)
    , m_fut(std::move(fut))
{}

// -----------------------------------------------------------------------------
// wall_now_us — monotonic wall clock in microseconds
// -----------------------------------------------------------------------------
double SimEngine::wall_now_us() const {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return timespec_to_us(ts);
}

// -----------------------------------------------------------------------------
// run — main simulation loop
// -----------------------------------------------------------------------------
void SimEngine::run() {
    const size_t chunk_size = m_config.chunk_size;
    const size_t channels_count = 0;  // resolved from file after open
    (void)channels_count;

    // -- Open IO --------------------------------------------------------------
    AudioReader reader(m_config.input_file);
    reader.open();

    const int    channels    = reader.channels();
    const int    file_rate   = reader.sample_rate();
    const size_t total_frames = reader.total_frames();

    // Warn if file sample rate differs from configured rate — we don't resample
    // in Tier 1 but the deadline computation uses m_config.sample_rate.
    if (file_rate != static_cast<int>(m_config.sample_rate)) {
        std::fprintf(stderr,
            "⚠  WARNING: input file sample rate (%d Hz) differs from config "
            "sample_rate (%zu Hz).\n"
            "   Deadline budget will be computed using config rate.\n"
            "   Audio will play at the wrong pitch/speed — resample your file "
            "or adjust sample_rate in config.\n",
            file_rate, m_config.sample_rate);
    }

    AudioWriter writer(m_config.output_file, channels, file_rate);
    writer.open();

    // -- Allocate buffers -----------------------------------------------------
    const size_t buf_samples = chunk_size * static_cast<size_t>(channels);
    std::vector<float> in_buf(buf_samples, 0.0f);
    std::vector<float> out_buf(buf_samples, 0.0f);

    const double budget_us = deadline_us(m_config);

    // -- Print run header -----------------------------------------------------
    const size_t estimated_chunks =
        (total_frames + chunk_size - 1) / chunk_size;

    std::printf("\n");
    std::printf("╔══════════════════════════════════════════════════════╗\n");
    std::printf("║                    pw-sim  Tier 1                   ║\n");
    std::printf("╚══════════════════════════════════════════════════════╝\n");
    std::printf("  input        : %s\n",   m_config.input_file.c_str());
    std::printf("  output       : %s\n",   m_config.output_file.c_str());
    std::printf("  format       : %s\n",   reader.format_description().c_str());
    std::printf("  sample rate  : %d Hz\n", file_rate);
    std::printf("  channels     : %d\n",   channels);
    std::printf("  chunk size   : %zu frames\n", chunk_size);
    std::printf("  deadline     : %.1f µs\n", budget_us);
    std::printf("  warmup       : %zu chunks\n", m_config.warmup_chunks);
    std::printf("  total frames : %zu  (~%zu chunks)\n",
                total_frames, estimated_chunks);
    std::printf("  clock mode   : SEQUENTIAL\n");
    std::printf("\n");
    std::printf("  %-8s  %-8s  %-10s  %-10s  %-8s  %s\n",
                "chunk", "frames", "wall(µs)", "budget(µs)", "ratio", "status");
    std::printf("  %s\n", std::string(64, '-').c_str());

    // -- Main loop ------------------------------------------------------------
    size_t chunk_index  = 0;   // counts ALL chunks including warmup
    size_t metric_index = 0;   // counts only non-warmup chunks

    while (true) {
        // Zero the buffers so tail padding is clean
        std::fill(in_buf.begin(),  in_buf.end(),  0.0f);
        std::fill(out_buf.begin(), out_buf.end(), 0.0f);

        // Read up to chunk_size frames
        size_t frames_read = reader.read(in_buf.data(), chunk_size);
        if (frames_read == 0) break;   // EOF

        // Zero-pad remainder of last chunk (tail policy: always zero-pad in T1)
        // in_buf was already zeroed above so padding is implicit.

        const bool is_warmup = (chunk_index < m_config.warmup_chunks);

        if (is_warmup) {
            // Run FUT to let it warm up (cache fill, JIT, model init, etc.)
            // but do NOT record metrics and write silence to output.
            m_fut(in_buf.data(), out_buf.data(),
                  frames_read, static_cast<size_t>(channels), chunk_index);
            writer.write_silence(frames_read);

            std::printf("  %-8zu  %-8zu  %-10s  %-10.1f  %-8s  [warmup]\n",
                        chunk_index, frames_read, "-", budget_us, "-");
        } else {
            // -- Time the FUT call -------------------------------------------
            double t_before = wall_now_us();

            m_fut(in_buf.data(), out_buf.data(),
                  frames_read, static_cast<size_t>(channels), chunk_index);

            double t_after  = wall_now_us();

            // -- Compute metrics ---------------------------------------------
            ChunkMetric m;
            m.chunk_index = metric_index;
            m.frames      = frames_read;
            m.wall_us     = t_after - t_before;
            m.deadline_us = budget_us;
            m.wall_ratio  = m.wall_us / budget_us;
            m.overrun     = (m.wall_us > budget_us);
            m.is_warmup   = false;

            m_metrics.push_back(m);
            print_chunk_log(m);

            // -- Write FUT output (always in Tier 1) -------------------------
            writer.write(out_buf.data(), frames_read);

            ++metric_index;
        }

        ++chunk_index;
    }

    // -- Close IO -------------------------------------------------------------
    reader.close();
    writer.close();

    // -- Print summary --------------------------------------------------------
    std::printf("  %s\n\n", std::string(64, '-').c_str());
    print_summary();
}

// -----------------------------------------------------------------------------
// print_chunk_log
// -----------------------------------------------------------------------------
void SimEngine::print_chunk_log(const ChunkMetric& m) const {
    const char* status = m.overrun ? "⚠ OVERRUN" : "OK";
    std::printf("  %-8zu  %-8zu  %-10.1f  %-10.1f  %-8.3f  %s\n",
                m.chunk_index,
                m.frames,
                m.wall_us,
                m.deadline_us,
                m.wall_ratio,
                status);
}

// -----------------------------------------------------------------------------
// print_summary
// -----------------------------------------------------------------------------
void SimEngine::print_summary() const {
    if (m_metrics.empty()) {
        std::printf("  No metrics collected.\n");
        return;
    }

    // Collect wall_us values for percentile computation
    std::vector<double> wall_vals;
    wall_vals.reserve(m_metrics.size());
    size_t overrun_count = 0;

    for (const auto& m : m_metrics) {
        wall_vals.push_back(m.wall_us);
        if (m.overrun) ++overrun_count;
    }

    // Sort for percentiles
    std::vector<double> sorted = wall_vals;
    std::sort(sorted.begin(), sorted.end());

    const size_t n   = sorted.size();
    const double p50 = sorted[n * 50 / 100];
    const double p95 = sorted[n * 95 / 100];
    const double p99 = sorted[n * 99 / 100];
    const double pmax = sorted.back();

    const double budget  = m_metrics[0].deadline_us;
    const double overrun_pct =
        100.0 * static_cast<double>(overrun_count) /
        static_cast<double>(n);

    // Verdict
    const char* verdict;
    const char* verdict_icon;
    if (overrun_count == 0) {
        verdict = "PASS";
        verdict_icon = "✅";
    } else if (overrun_pct < 1.0) {
        verdict = "MARGINAL";
        verdict_icon = "⚠ ";
    } else {
        verdict = "FAIL";
        verdict_icon = "❌";
    }

    std::printf("╔══════════════════════════════════════════════════════╗\n");
    std::printf("║                      Summary                        ║\n");
    std::printf("╚══════════════════════════════════════════════════════╝\n");
    std::printf("  chunks measured : %zu\n", n);
    std::printf("  deadline budget : %.1f µs\n", budget);
    std::printf("\n");
    std::printf("  wall latency\n");
    std::printf("    p50  : %8.1f µs   (%.2fx budget)\n", p50,  p50  / budget);
    std::printf("    p95  : %8.1f µs   (%.2fx budget)\n", p95,  p95  / budget);
    std::printf("    p99  : %8.1f µs   (%.2fx budget)\n", p99,  p99  / budget);
    std::printf("    max  : %8.1f µs   (%.2fx budget)\n", pmax, pmax / budget);
    std::printf("\n");
    std::printf("  overruns        : %zu / %zu  (%.2f%%)\n",
                overrun_count, n, overrun_pct);
    std::printf("\n");
    std::printf("  verdict         : %s %s\n", verdict_icon, verdict);
    std::printf("\n");

    // Extra hint when marginal or failing
    if (overrun_count > 0) {
        std::printf("  ℹ  In REALTIME mode (Tier 2), these %zu overrun chunk(s)\n"
                    "     would produce audible glitches in the output audio.\n\n",
                    overrun_count);
    }
}
