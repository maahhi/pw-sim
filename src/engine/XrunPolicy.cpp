#include "engine/XrunPolicy.hpp"
#include <cstring>  // memset, memcpy

void apply_xrun_policy(
    XrunPolicy                policy,
    float*                    output,
    const float*              input,
    const std::vector<float>& last_good,
    size_t                    frames,
    size_t                    channels)
{
    const size_t total = frames * channels;

    switch (policy) {

    case XrunPolicy::ZEROS:
        // ── Silence ──────────────────────────────────────────────────────────
        // What PipeWire actually does: the output buffer was pre-zeroed at the
        // start of the period. FUT's late result is discarded.
        // The user hears a pop/dropout — immediately obvious, easy to count.
        std::memset(output, 0, total * sizeof(float));
        break;

    case XrunPolicy::REPEAT_LAST:
        // ── Repeat last good chunk ────────────────────────────────────────────
        // Copy the most recent chunk where FUT finished on time.
        // Sounds less jarring than silence for a single overrun, but repeated
        // overruns produce a "stuck loop" artifact.
        // Falls back to zeros if no good chunk exists yet (very first chunk).
        if (last_good.size() >= total) {
            std::memcpy(output, last_good.data(), total * sizeof(float));
        } else {
            std::memset(output, 0, total * sizeof(float));
        }
        break;

    case XrunPolicy::PASSTHROUGH:
        // ── Dry passthrough ───────────────────────────────────────────────────
        // Copy the input chunk (unprocessed audio) to output.
        // Simulates a plugin that did an optimistic memcpy(output, input) first,
        // then started overwriting with processed audio — if it overruns, the
        // output already has dry audio rather than silence.
        // Not what PipeWire does, but useful for diagnosing processing gaps.
        std::memcpy(output, input, total * sizeof(float));
        break;
    }
}
