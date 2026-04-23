#include "SimConfig.hpp"
#include "engine/SimEngine.hpp"
#include "fut/FutInterface.hpp"
#include "fut/stubs/PassthroughFut.hpp"
#include "fut/stubs/GainFut.hpp"
#include "fut/stubs/SlowFut.hpp"

#include <cstdio>
#include <cstdlib>     // EXIT_SUCCESS, EXIT_FAILURE
#include <stdexcept>
#include <string>

// =============================================================================
// pw-sim  —  Tier 1 entry point
//
// HOW TO SWITCH THE FUT:
//   Edit the "Active FUT" section below. Choose one of the three stubs, or
//   replace it with your own lambda / function that matches FutFn's signature.
//
// HOW TO CHANGE CONFIG:
//   Edit the SimConfig fields below. In Tier 3 this will be a TOML file + CLI.
// =============================================================================

// -----------------------------------------------------------------------------
// Config — edit these values, recompile, re-run
// -----------------------------------------------------------------------------
static SimConfig make_config(int argc, char* argv[]) {
    SimConfig cfg;

    // IO — override via first two positional args if provided:
    //   ./pw-sim input.wav output.wav
    if (argc >= 2) cfg.input_file  = argv[1];
    if (argc >= 3) cfg.output_file = argv[2];

    // Engine
    cfg.chunk_size    = 256;     // frames per FUT call
    cfg.sample_rate   = 48000;   // Hz — used for deadline computation only
    cfg.warmup_chunks = 4;       // chunks to run before measuring

    return cfg;
}

// -----------------------------------------------------------------------------
// Active FUT — comment/uncomment to choose, or write your own below
// -----------------------------------------------------------------------------
static FutFn make_active_fut() {

    // ── Option 1: Passthrough ────────────────────────────────────────────────
    // Output == input. Use to verify the simulator pipeline is correct.
    // Expected: output audio sounds identical to input, 0 overruns.
    //
    //return make_passthrough_fut();

    // ── Option 2: Gain ───────────────────────────────────────────────────────
    // Attenuates or amplifies. Use to verify audio alteration works.
    // Expected: output sounds quieter/louder, 0 overruns.
    //
    //return make_gain_fut(/*gain=*/0.1f);

    // ── Option 3: Slow ───────────────────────────────────────────────────────
    // Passthrough with an artificial sleep to trigger overruns.
    // Use to verify overrun detection and (in Tier 2) xrun policy.
    //
    // delay_us=8000 with chunk=256/48kHz (budget=5333µs) → every chunk overruns
    // slow_every_n=20 → only every 20th chunk overruns (~5% overrun rate)
    //
     return make_slow_fut(/*delay_us=*/8000, /*slow_every_n=*/0);
    // return make_slow_fut(/*delay_us=*/8000, /*slow_every_n=*/20);

    // ── Option 4: Your FUT ───────────────────────────────────────────────────
    // Replace with your ML model or any function matching FutFn:
    //
    // return [](const float* input, float* output,
    //           size_t frames, size_t channels, size_t chunk_index)
    // {
    //     // your processing here
    //     (void)chunk_index;
    //     for (size_t i = 0; i < frames * channels; ++i)
    //         output[i] = input[i];  // replace with real processing
    // };
}

// -----------------------------------------------------------------------------
// main
// -----------------------------------------------------------------------------
int main(int argc, char* argv[]) {
    try {
        SimConfig cfg = make_config(argc, argv);
        FutFn     fut = make_active_fut();

        SimEngine engine(cfg, std::move(fut));
        engine.run();

        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "\n❌ pw-sim error: %s\n\n", e.what());
        return EXIT_FAILURE;
    }
}
