#pragma once
#include <functional>
#include <cstddef>

// =============================================================================
// pw-sim  —  Function Under Test (FUT) Interface
//
// This is the contract that every function being tested must satisfy.
// It mirrors what a PipeWire filter node's process() callback receives.
//
// Rules (same as a real PipeWire plugin):
//   - Must return before the deadline (chunk_size / sample_rate seconds).
//   - Must not block (no file IO, no mutex wait, no sleep).
//   - Must not allocate heap memory on the hot path if possible.
//   - Input and output buffers are valid only for the duration of the call.
//   - Input and output are NON-OVERLAPPING — do not write to input.
//
// Buffer layout:
//   Interleaved float32.
//   For stereo: [L0, R0, L1, R1, ..., L(frames-1), R(frames-1)]
//   Total floats in each buffer: frames * channels
//
// Parameters:
//   input        — read-only input buffer (dry audio from the graph)
//   output       — write-only output buffer (your processed audio)
//   frames       — number of PCM frames (== chunk_size, except last chunk)
//   channels     — number of audio channels (1=mono, 2=stereo, ...)
//   chunk_index  — monotonically increasing call counter (0-based)
//                  useful for stateful models that need to track position
// =============================================================================

using FutFn = std::function<void(
    const float* input,
    float*       output,
    size_t       frames,
    size_t       channels,
    size_t       chunk_index
)>;
