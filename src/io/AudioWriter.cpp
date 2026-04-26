#include "io/AudioWriter.hpp"
#include <stdexcept>
#include <vector>
#include <cstring>  // memset

AudioWriter::AudioWriter(const std::string& path, int channels, int sample_rate,
                         int subformat)
    : m_path(path)
    , m_channels(channels)
    , m_sample_rate(sample_rate)
    , m_subformat(subformat)
{}

AudioWriter::~AudioWriter() {
    close();
}

void AudioWriter::open() {
    m_info = {};
    m_info.samplerate = m_sample_rate;
    m_info.channels   = m_channels;
    // Tier 1: always write float32 WAV.
    // Tier 3: this will be driven by SimConfig::output_format.
    m_info.format     = SF_FORMAT_WAV | m_subformat;

    m_file = sf_open(m_path.c_str(), SFM_WRITE, &m_info);
    if (!m_file) {
        throw std::runtime_error(
            "AudioWriter: cannot open '" + m_path + "' for writing: " +
            std::string(sf_strerror(nullptr))
        );
    }
}

void AudioWriter::write(const float* buffer, size_t frames) {
    if (!m_file) return;

    sf_count_t written = sf_writef_float(m_file, buffer,
                                         static_cast<sf_count_t>(frames));
    if (written != static_cast<sf_count_t>(frames)) {
        throw std::runtime_error(
            "AudioWriter: short write — expected " + std::to_string(frames) +
            " frames, wrote " + std::to_string(written)
        );
    }
}

void AudioWriter::write_silence(size_t frames) {
    // Stack-allocate for small chunks; heap-allocate for large ones.
    const size_t total = frames * static_cast<size_t>(m_channels);
    std::vector<float> silence(total, 0.0f);
    write(silence.data(), frames);
}

void AudioWriter::close() {
    if (m_file) {
        sf_write_sync(m_file);
        sf_close(m_file);
        m_file = nullptr;
    }
}
