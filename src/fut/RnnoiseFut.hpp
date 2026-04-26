#pragma once
#include <algorithm>
#include <memory>
#include <vector>
#include "FutInterface.hpp"

extern "C" {
#include "rnnoise.h"
}

// RNNoise (xiph/rnnoise) noise suppression FUT.
//
// RNNoise processes mono frames of exactly rnnoise_get_frame_size() samples
// (480 at 48 kHz = 10 ms) with floats in the ±32768 range.  This wrapper
// handles arbitrary chunk sizes and any channel count by:
//   1. Deinterleaving and scale-converting input into per-channel FIFOs.
//   2. Running rnnoise_process_frame() whenever ≥ 480 samples are queued.
//   3. Interleaving the processed output back (silence for initial latency).
//
// One-frame latency (≤ 480 samples / 10 ms) is introduced on the first pass.
// The state is shared across calls via a shared_ptr held in the closure.

inline FutFn make_rnnoise_fut() {
    struct Queue {
        std::vector<float> buf;
        size_t rd = 0;

        void push(float v)   { buf.push_back(v); }
        float pop()          { return buf[rd++]; }
        size_t avail() const { return buf.size() - rd; }
        void compact() {
            if (rd == 0) return;
            buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(rd));
            rd = 0;
        }
    };

    struct State {
        int frame_size;
        size_t channels = 0;
        std::vector<DenoiseState*> dn;
        std::vector<Queue> ibuf, obuf;
        std::vector<float> tmp_in, tmp_out;

        explicit State(int fs) : frame_size(fs) {}
        ~State() { for (auto* d : dn) if (d) rnnoise_destroy(d); }

        void init(size_t ch) {
            for (auto* d : dn) if (d) rnnoise_destroy(d);
            channels = ch;
            dn.assign(ch, nullptr);
            for (size_t c = 0; c < ch; ++c) dn[c] = rnnoise_create(nullptr);
            ibuf.assign(ch, {});
            obuf.assign(ch, {});
            tmp_in.assign(static_cast<size_t>(frame_size), 0.0f);
            tmp_out.assign(static_cast<size_t>(frame_size), 0.0f);
        }
    };

    auto st = std::make_shared<State>(rnnoise_get_frame_size()); // Todo: make it non-heap

    return [st](const float* input, float* output,
                size_t frames, size_t channels, size_t /*chunk_index*/) {
        if (st->channels != channels) st->init(channels);

        constexpr float S    = 32768.0f;
        constexpr float S_INV = 1.0f / 32768.0f;
        const int fs = st->frame_size;

        for (size_t c = 0; c < channels; ++c) {
            st->ibuf[c].compact();
            st->obuf[c].compact();
        }

        // Deinterleave, scale to ±32768, and enqueue
        for (size_t f = 0; f < frames; ++f)
            for (size_t c = 0; c < channels; ++c)
                st->ibuf[c].push(input[f * channels + c] * S);

        // Process complete 480-sample frames per channel
        for (size_t c = 0; c < channels; ++c) {
            while (static_cast<int>(st->ibuf[c].avail()) >= fs) {
                for (int i = 0; i < fs; ++i) st->tmp_in[i] = st->ibuf[c].pop();
                rnnoise_process_frame(st->dn[c], st->tmp_out.data(), st->tmp_in.data());
                for (int i = 0; i < fs; ++i) st->obuf[c].push(st->tmp_out[i] * S_INV);
            }
        }

        // Reinterleave; pad with silence during initial buffering latency
        for (size_t f = 0; f < frames; ++f)
            for (size_t c = 0; c < channels; ++c)
                output[f * channels + c] =
                    st->obuf[c].avail() > 0 ? st->obuf[c].pop() : 0.0f;
    };
}
