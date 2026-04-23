#include "io/AudioReader.hpp"
#include <stdexcept>
#include <string>

AudioReader::AudioReader(const std::string& path)
    : m_path(path)
{}

AudioReader::~AudioReader() {
    close();
}

void AudioReader::open() {
    m_info = {};  // zero-init required by libsndfile

    m_file = sf_open(m_path.c_str(), SFM_READ, &m_info);
    if (!m_file) {
        throw std::runtime_error(
            "AudioReader: cannot open '" + m_path + "': " +
            std::string(sf_strerror(nullptr))
        );
    }
}

size_t AudioReader::read(float* buffer, size_t frames) {
    if (!m_file) return 0;

    // sf_readf_float reads `frames` frames (each frame = channels floats).
    // Returns actual frames read; 0 = EOF.
    sf_count_t got = sf_readf_float(m_file, buffer,
                                    static_cast<sf_count_t>(frames));
    return static_cast<size_t>(got);
}

void AudioReader::close() {
    if (m_file) {
        sf_close(m_file);
        m_file = nullptr;
    }
}

std::string AudioReader::format_description() const {
    // libsndfile does not expose a human-readable format string directly,
    // so we build a minimal one from the SF_INFO format field.
    SF_FORMAT_INFO fi = {};
    fi.format = m_info.format & SF_FORMAT_TYPEMASK;
    sf_command(nullptr, SFC_GET_FORMAT_INFO, &fi, sizeof(fi));

    SF_FORMAT_INFO si = {};
    si.format = m_info.format & SF_FORMAT_SUBMASK;
    sf_command(nullptr, SFC_GET_FORMAT_INFO, &si, sizeof(si));

    std::string desc;
    if (fi.name) desc += fi.name;
    desc += " / ";
    if (si.name) desc += si.name;
    return desc;
}
