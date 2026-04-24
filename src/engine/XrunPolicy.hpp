#pragma once
#include "SimConfig.hpp"
#include <cstddef>
#include <cstring>   // memcpy, memset
#include <vector>

// =============================================================================
// XrunPolicy implementation
//
// Called by SimEngine in REALTIME mode when FUT misses its deadline.
// Replaces the output buffer with the appropriate fallback audio.
//
// All functions write exactly `frames * channels` floats into `output`.
// =============================================================================

// Apply the configured xrun policy to the output buffer.
//
//   policy       : which strategy to use
//   output       : buffer to overwrite (FUT's result — discarded)
//   input        : the dry input for this chunk (used by PASSTHROUGH)
//   last_good    : the last chunk where FUT finished on time (used by REPEAT_LAST)
//                  may be empty on the first chunk — falls back to zeros
//   frames       : number of PCM frames
//   channels     : number of audio channels
void apply_xrun_policy(
    XrunPolicy          policy,
    float*              output,
    const float*        input,
    const std::vector<float>& last_good,
    size_t              frames,
    size_t              channels
);
