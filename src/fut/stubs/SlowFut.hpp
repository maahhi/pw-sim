#pragma once
#include "fut/FutInterface.hpp"
#include <cstring>      // memcpy
#include <thread>       // this_thread::sleep_for
#include <chrono>
#include <cstdio>       // printf

// =============================================================================
// SlowFut
//
// Passthrough with a configurable artificial delay before returning.
// Deliberately designed to trigger overruns so you can verify that:
//   - The log correctly flags overrun=YES for affected chunks.
//   - wall_ratio > 1.0 is reported.
//   - In Tier 2 REALTIME mode: cumulative debt grows and xrun policy fires.
//
// Use for:
//   - Testing overrun detection in SEQUENTIAL mode (Tier 1).
//   - Testing xrun policy audio output in REALTIME mode (Tier 2).
//   - Hearing what deadline violations sound like in the output file.
//
// Parameters:
//   delay_us        — microseconds to sleep before returning on every chunk
//   slow_every_n    — only sleep every N chunks (0 = always slow)
//                     e.g. slow_every_n=10 means chunk 0,10,20,... are slow
//                     useful to hear intermittent glitches vs constant overrun
//
// Example: chunk_size=256, sample_rate=48000 → deadline=5333us
//   delay_us=8000, slow_every_n=0  → every chunk overruns (~1.5x deadline)
//   delay_us=8000, slow_every_n=20 → ~5% of chunks overrun
// =============================================================================

inline FutFn make_slow_fut(long delay_us = 8000, size_t slow_every_n = 0) {
    return [delay_us, slow_every_n](
                  const float* input,
                  float*       output,
                  size_t       frames,
                  size_t       channels,
                  size_t       chunk_index)
    {
        // Determine whether this chunk gets the artificial delay
        bool be_slow = (slow_every_n == 0) ||
                       (chunk_index % slow_every_n == 0);

        if (be_slow) {
            std::this_thread::sleep_for(std::chrono::microseconds(delay_us));
        }

        // Still do passthrough — the point is timing, not corruption
        std::memcpy(output, input, frames * channels * sizeof(float));
    };
}
