#pragma once
#include "fut/FutInterface.hpp"
#include <cstring>  // memcpy

// =============================================================================
// PassthroughFut
//
// The simplest possible FUT: copies input to output unchanged.
//
// Use for:
//   - Verifying the simulator pipeline is correct end-to-end.
//   - The output audio file should be perceptually identical to the input.
//   - Latency should be minimal (~memcpy time, a few microseconds).
//   - Zero overruns expected for any reasonable chunk size.
// =============================================================================

inline FutFn make_passthrough_fut() {
    return [](const float* input,
              float*       output,
              size_t       frames,
              size_t       channels,
              size_t       /*chunk_index*/)
    {
        std::memcpy(output, input, frames * channels * sizeof(float));
    };
}
