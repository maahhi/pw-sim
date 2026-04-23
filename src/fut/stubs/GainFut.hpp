#pragma once
#include "fut/FutInterface.hpp"

// =============================================================================
// GainFut
//
// Multiplies every sample by a fixed gain factor.
//
// Use for:
//   - Verifying the simulator correctly passes altered audio to the output.
//   - The output file should sound louder (gain > 1.0) or quieter (gain < 1.0).
//   - Latency should be minimal (a simple multiply loop).
//   - Zero overruns expected.
//
// Parameters:
//   gain — linear amplitude multiplier
//          0.5  = -6 dB  (half amplitude)
//          1.0  = 0 dB   (unity, same as passthrough)
//          2.0  = +6 dB  (double amplitude, may clip if input is loud)
//
// WARNING: gain > 1.0 on a full-scale signal will clip to [-1, 1] range.
//          Clipping is intentional — it tests that the output pipeline handles
//          out-of-range floats the same way a real DAC driver would.
// =============================================================================

inline FutFn make_gain_fut(float gain = 0.5f) {
    return [gain](const float* input,
                  float*       output,
                  size_t       frames,
                  size_t       channels,
                  size_t       /*chunk_index*/)
    {
        const size_t total_samples = frames * channels;
        for (size_t i = 0; i < total_samples; ++i) {
            output[i] = input[i] * gain;
        }
    };
}
