#pragma once
#include <string>
#include <cstddef>
#include <sndfile.h>

// =============================================================================
// AudioWriter
//
// Wraps libsndfile to write interleaved float32 samples to a WAV file.
//
// `subformat` is a libsndfile SF_FORMAT_* subtype constant:
//   SF_FORMAT_FLOAT  — 32-bit float  (default)
//   SF_FORMAT_PCM_16 — signed 16-bit integer
//   SF_FORMAT_PCM_24 — signed 24-bit integer
// Always writes WAV container regardless of subformat.
//
// IMPORTANT: write() must be called in chunk order with no gaps.
// The writer does NOT reorder or buffer — what you write is what lands in
// the file. This is intentional: glitch artifacts are preserved as-is.
//
// Usage:
//   AudioWriter writer("output.wav", /*channels=*/2, /*sample_rate=*/48000,
//                      SF_FORMAT_PCM_16);
//   writer.open();
//   writer.write(buffer, frames);
//   writer.close();
// =============================================================================

class AudioWriter {
public:
    AudioWriter(const std::string& path, int channels, int sample_rate,
                int subformat = SF_FORMAT_FLOAT);
    ~AudioWriter();

    // Opens the file for writing. Throws std::runtime_error on failure.
    void open();

    // Writes `frames` frames from `buffer` (interleaved float32).
    // Throws std::runtime_error if the write fails.
    void write(const float* buffer, size_t frames);

    // Writes `frames` frames of silence (zeros).
    // Used for warmup chunk placeholders.
    void write_silence(size_t frames);

    // Closes and finalises the file. Safe to call multiple times.
    void close();

private:
    std::string m_path;
    int         m_channels;
    int         m_sample_rate;
    int         m_subformat;
    SNDFILE*    m_file = nullptr;
    SF_INFO     m_info = {};
};
