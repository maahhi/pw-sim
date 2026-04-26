#pragma once
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#include "get_dsp.h"
#pragma GCC diagnostic pop

#include "FutInterface.hpp"

// NAM (Neural Amp Modeler) FUT.
//
// Loads a .nam model file (WaveNet, LSTM, ConvNet, or Linear architecture) and
// runs it via NeuralAmpModelerCore (Eigen-backed). RTNeural is available as an
// alternative inference backend in third_party/RTNeural for future use.
//
// NAM models are mono (1-in 1-out). Stereo audio is handled by running one
// independent DSP instance per channel, each prewarm'd at construction time.
//
// NAM_SAMPLE is `double` by default. We convert float ↔ double per-chunk.

inline FutFn make_nam_fut(const std::string& model_path, double sample_rate = 48000.0) {
    std::ifstream f(model_path);
    if (!f.is_open())
        throw std::runtime_error("NAM: cannot open model file: " + model_path);
    nlohmann::json j;
    f >> j;

    // Verify the file is valid before caching JSON
    {
        auto probe = nam::get_dsp(j);
        if (!probe)
            throw std::runtime_error("NAM: get_dsp() returned null for: " + model_path);
    }

    std::fprintf(stderr, "[pw-sim] FUT: NAM  model=%s  sr=%.0f Hz\n",
                 model_path.c_str(), sample_rate);

    struct State {
        nlohmann::json config;
        double sr;
        size_t channels = 0;
        std::vector<std::unique_ptr<nam::DSP>> dsp;   // one per channel
        std::vector<std::vector<NAM_SAMPLE>> ibuf;     // planar input per channel
        std::vector<std::vector<NAM_SAMPLE>> obuf;     // planar output per channel
        std::vector<NAM_SAMPLE*> iptr, optr;           // raw pointers for process()

        void init(size_t ch, double sr_) {
            sr = sr_;
            channels = ch;
            dsp.clear();
            for (size_t c = 0; c < ch; ++c) {
                auto d = nam::get_dsp(config);
                d->ResetAndPrewarm(sr, NAM_DEFAULT_MAX_BUFFER_SIZE);
                dsp.push_back(std::move(d));
            }
            ibuf.assign(ch, {});
            obuf.assign(ch, {});
            iptr.resize(ch);
            optr.resize(ch);
        }

        void ensure(size_t frames) {
            for (size_t c = 0; c < channels; ++c) {
                if (ibuf[c].size() < frames) {
                    ibuf[c].resize(frames);
                    obuf[c].resize(frames);
                }
                iptr[c] = ibuf[c].data();
                optr[c] = obuf[c].data();
            }
        }
    };

    auto st = std::make_shared<State>();
    st->config = std::move(j);
    st->sr = sample_rate;

    return [st, sample_rate](const float* input, float* output,
                              size_t frames, size_t channels, size_t /*chunk_index*/) {
        if (st->channels != channels)
            st->init(channels, sample_rate);

        st->ensure(frames);

        // Deinterleave and convert float → NAM_SAMPLE (double)
        for (size_t f = 0; f < frames; ++f)
            for (size_t c = 0; c < channels; ++c)
                st->ibuf[c][f] = static_cast<NAM_SAMPLE>(input[f * channels + c]);

        // Process each channel through its own DSP instance
        for (size_t c = 0; c < channels; ++c)
            st->dsp[c]->process(&st->iptr[c], &st->optr[c], static_cast<int>(frames));

        // Reinterleave and convert NAM_SAMPLE → float
        for (size_t f = 0; f < frames; ++f)
            for (size_t c = 0; c < channels; ++c)
                output[f * channels + c] = static_cast<float>(st->obuf[c][f]);
    };
}
