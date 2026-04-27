#pragma once
#ifdef PW_SIM_HAVE_RUBBERBAND

#include "fut/FutInterface.hpp"
#include <rubberband/RubberBandStretcher.h>

#include <memory>
#include <vector>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <algorithm>

// Rubberband real-time pitch-shift FUT.
//
// Uses the R2 real-time engine (OptionProcessRealTime) with
// OptionPitchHighConsistency, time ratio fixed at 1.0 (no time-stretching).
//
// Rubberband's API is planar (float**), so this wrapper deinterleaves the
// FUT's interleaved input into per-channel FIFOs, feeds the stretcher
// based on getSamplesRequired(), accumulates retrieved output, then
// reinterleaves back.  Zero-fill is output during initial engine latency
// (~85 ms at 48 kHz with the R2 engine).

inline FutFn make_rubberband_fut(size_t sample_rate, double semitones) {

    struct Chan {
        std::vector<float> buf;
        size_t rd = 0;
        size_t avail() const { return buf.size() - rd; }
        void compact() {
            if (!rd) return;
            buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(rd));
            rd = 0;
        }
    };

    struct State {
        using RBS = RubberBand::RubberBandStretcher;
        std::unique_ptr<RBS> rb;
        size_t channels    = 0;
        size_t sample_rate;
        double semitones;

        std::vector<Chan>         in_ch;
        std::vector<Chan>         out_ch;
        std::vector<std::vector<float>> plane;  // scratch reused for in then out
        std::vector<const float*> in_ptrs;
        std::vector<float*>       out_ptrs;

        State(size_t sr, double s) : sample_rate(sr), semitones(s) {}

        void init(size_t ch) {
            channels = ch;
            double pitch_scale = std::pow(2.0, semitones / 12.0);
            auto opts = RBS::OptionProcessRealTime |
                        RBS::OptionPitchHighConsistency;
            rb = std::make_unique<RBS>(sample_rate, ch, opts, 1.0, pitch_scale);

            in_ch.assign(ch, {});
            out_ch.assign(ch, {});
            plane.assign(ch, {});
            in_ptrs.resize(ch);
            out_ptrs.resize(ch);

            std::fprintf(stderr,
                "[RubberbandFut] init: channels=%zu  sample_rate=%zu  "
                "semitones=%.2f  pitch_scale=%.6f  engine_latency=%zu samples\n",
                ch, sample_rate, semitones, pitch_scale, rb->getLatency());
        }
    };

    auto st = std::make_shared<State>(sample_rate, semitones);

    std::fprintf(stderr,
        "[RubberbandFut] semitones=%.2f  pitch_scale=%.6f  sample_rate=%zu"
        "  (engine init deferred to first call)\n",
        semitones, std::pow(2.0, semitones / 12.0), sample_rate);

    return [st](const float* input, float* output,
                size_t frames, size_t channels, size_t /*chunk_index*/) {
        if (st->channels != channels) st->init(channels);

        const size_t ch = channels;

        for (size_t c = 0; c < ch; ++c) {
            st->in_ch[c].compact();
            st->out_ch[c].compact();
        }

        // Deinterleave input into per-channel FIFOs
        for (size_t f = 0; f < frames; ++f)
            for (size_t c = 0; c < ch; ++c)
                st->in_ch[c].buf.push_back(input[f * ch + c]);

        // Feed rubberband until it stops requesting samples or input runs dry
        for (;;) {
            size_t need = st->rb->getSamplesRequired();
            if (need == 0) break;
            bool enough = true;
            for (size_t c = 0; c < ch; ++c)
                if (st->in_ch[c].avail() < need) { enough = false; break; }
            if (!enough) break;

            for (size_t c = 0; c < ch; ++c) {
                st->plane[c].resize(need);
                std::memcpy(st->plane[c].data(),
                            st->in_ch[c].buf.data() + st->in_ch[c].rd,
                            need * sizeof(float));
                st->in_ch[c].rd += need;
                st->in_ptrs[c] = st->plane[c].data();
            }
            st->rb->process(st->in_ptrs.data(), need, false);
        }

        // Drain all available output frames into per-channel out FIFOs
        for (;;) {
            int avail = st->rb->available();
            if (avail <= 0) break;
            size_t n = static_cast<size_t>(avail);
            for (size_t c = 0; c < ch; ++c) {
                st->plane[c].resize(n);
                st->out_ptrs[c] = st->plane[c].data();
            }
            size_t got = st->rb->retrieve(st->out_ptrs.data(), n);
            for (size_t c = 0; c < ch; ++c)
                st->out_ch[c].buf.insert(st->out_ch[c].buf.end(),
                                         st->plane[c].data(),
                                         st->plane[c].data() + got);
        }

        // Interleave output to caller; zero-fill during initial engine latency
        for (size_t f = 0; f < frames; ++f)
            for (size_t c = 0; c < ch; ++c)
                output[f * ch + c] =
                    st->out_ch[c].avail() > 0
                        ? st->out_ch[c].buf[st->out_ch[c].rd++]
                        : 0.0f;
    };
}

#endif // PW_SIM_HAVE_RUBBERBAND
