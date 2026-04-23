#pragma once
#include <string>
#include <cstddef>
#include <sndfile.h>

// =============================================================================
// AudioWriter
//
// Wraps libsndfile to write interleaved float32 samples to a WAV file.
// In Tier 1 the output is always float32 WAV. Tier 3 will add format control.
//
// IMPORTANT: write() must be called in chunk order with no gaps.
// The writer does NOT reorder or buffer — what you write is what lands in
// the file. This is intentional: glitch artifacts are preserved as-is.
//
// Usage:
//   AudioWriter writer("output.wav", /*channels=*/2, /*sample_rate=*/48000);
//   writer.open();
//   writer.write(buffer, frames);
//   writer.close();
// =============================================================================

class AudioWriter {
public:
    AudioWriter(const std::string& path, int channels, int sample_rate);
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
    SNDFILE*    m_file = nullptr;
    SF_INFO     m_info = {};
};
