#pragma once
#include <string>
#include <cstddef>
#include <sndfile.h>

// =============================================================================
// AudioReader
//
// Wraps libsndfile to read any supported audio file format as interleaved
// float32 samples. The caller never deals with bit depth or encoding —
// libsndfile converts everything internally.
//
// Usage:
//   AudioReader reader("input.wav");
//   reader.open();
//   std::vector<float> buf(reader.channels() * chunk_size);
//   size_t got = reader.read(buf.data(), chunk_size);
//   // got <= chunk_size; got == 0 means EOF
//   reader.close();
// =============================================================================

class AudioReader {
public:
    explicit AudioReader(const std::string& path);
    ~AudioReader();

    // Opens the file. Throws std::runtime_error on failure.
    void open();

    // Reads up to `frames` frames into `buffer` (interleaved float32).
    // Returns the number of frames actually read (< frames at EOF).
    // Returns 0 at EOF.
    size_t read(float* buffer, size_t frames);

    // Closes the file. Safe to call multiple times.
    void close();

    // Metadata — valid after open()
    int    channels()    const { return m_info.channels; }
    int    sample_rate() const { return m_info.samplerate; }
    size_t total_frames() const { return static_cast<size_t>(m_info.frames); }
    std::string format_description() const;

private:
    std::string m_path;
    SNDFILE*    m_file = nullptr;
    SF_INFO     m_info = {};
};
