#pragma once
#include <cstddef>
#include <string>

// =============================================================================
// pw-sim  —  Tier 1 Configuration
//
// All knobs live here. In Tier 3 this struct will be populated from a TOML
// file + CLI overrides. For now, edit these constants and recompile.
// =============================================================================

struct SimConfig {

    // -------------------------------------------------------------------------
    // IO
    // -------------------------------------------------------------------------
    std::string input_file  = "input.wav";   // path to source audio
    std::string output_file = "output.wav";  // path for result audio

    // -------------------------------------------------------------------------
    // Engine
    // -------------------------------------------------------------------------

    // Number of PCM frames handed to FUT per callback.
    // Common PipeWire values: 64, 128, 256, 512, 1024
    size_t chunk_size = 256;

    // Sample rate in Hz. Must match the input file or resampling is needed
    // (resampling is NOT done in Tier 1 — file rate is used as-is and this
    //  value is only used to compute the deadline budget).
    size_t sample_rate = 48000;

    // Chunks to run through FUT before recording any metrics or writing output.
    // Useful to let ML models warm up (JIT compile, cache fill, etc.).
    // Warmup chunk outputs are written as silence in the output file so that
    // chunk indices in the log match time positions in the audio file.
    size_t warmup_chunks = 4;

    // -------------------------------------------------------------------------
    // Deadline
    // -------------------------------------------------------------------------

    // Computed from chunk_size / sample_rate — shown here for clarity.
    // deadline_us = (chunk_size / sample_rate) * 1e6
    // e.g. 256 / 48000 * 1e6 = 5333 µs
    //
    // In Tier 1 the deadline is used for overrun DETECTION only — it does not
    // affect what gets written to the output file (that comes in Tier 2).

    // -------------------------------------------------------------------------
    // Tail policy
    // -------------------------------------------------------------------------

    // What to do when the last read is shorter than chunk_size.
    // In Tier 1: always zero-pad. Tier 3 will add a "drop" option.
    // (no field needed yet — behaviour is fixed)
};

// Convenience: compute deadline in microseconds from config
inline double deadline_us(const SimConfig& cfg) {
    return (static_cast<double>(cfg.chunk_size) /
            static_cast<double>(cfg.sample_rate)) * 1e6;
}
