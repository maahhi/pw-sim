#pragma once
#include "engine/ChunkMetric.hpp"
#include <string>
#include <vector>
#include <cstdio>

// =============================================================================
// MetricsWriter
//
// Writes per-chunk metrics to a CSV file and prints a summary to stdout.
//
// CSV schema:
//   chunk_idx, frames, wall_us, cpu_us, deadline_us, overrun, wall_ratio,
//   xrun_applied, cumulative_debt_us,
//   vol_ctx_sw, invol_ctx_sw, page_faults_minor, page_faults_major
//
// One row per non-warmup chunk. Warmup chunks are excluded.
// =============================================================================

class MetricsWriter {
public:
    explicit MetricsWriter(const std::string& csv_path);
    ~MetricsWriter();

    // Open the CSV file and write the header row.
    // Throws std::runtime_error on failure.
    void open();

    // Append one row for a completed chunk. Call once per non-warmup chunk.
    void write_chunk(const ChunkMetric& m);

    // Close the CSV file.
    void close();

    // Print a full summary to stdout after all chunks are processed.
    // metrics : all non-warmup ChunkMetric records collected by SimEngine.
    // clock_mode_str : "SEQUENTIAL" or "REALTIME" — shown in header.
    static void print_summary(const std::vector<ChunkMetric>& metrics,
                               const std::string& clock_mode_str);

private:
    std::string m_path;
    FILE*       m_file = nullptr;
};
