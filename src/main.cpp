#include "SimConfig.hpp"
#include "engine/SimEngine.hpp"
#include "fut/FutInterface.hpp"
#include "fut/stubs/PassthroughFut.hpp"
#include "fut/stubs/GainFut.hpp"
#include "fut/stubs/SlowFut.hpp"
#include "fut/RnnoiseFut.hpp"
#include "fut/NamFut.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

// =============================================================================
// pw-sim  —  Tier 2 entry point
//
// HOW TO SWITCH THE FUT:
//   Edit make_active_fut() below, or pass --rnnoise / --nam <model.nam> on the CLI.
//
// HOW TO CHANGE CONFIG:
//   Edit make_config() below. Tier 3 will load this from a TOML file + CLI.
// =============================================================================

// Strip a boolean flag from argv in-place; returns true if found.
static bool consume_flag(int& argc, char* argv[], const char* flag) {
    bool found = false;
    int j = 0;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) { found = true; }
        else                                 { argv[j++] = argv[i]; }
    }
    argc = j;
    return found;
}

// Strip a value flag (--flag VALUE) from argv; returns the value, or "" if not found.
static std::string consume_value_flag(int& argc, char* argv[], const char* flag) {
    std::string value;
    int j = 0;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0 && i + 1 < argc) {
            value = argv[++i];
        } else {
            argv[j++] = argv[i];
        }
    }
    argc = j;
    return value;
}

static SimConfig make_config(int argc, char* argv[]) {
    SimConfig cfg;

    // Positional args: ./pw-sim [input.wav] [output.wav]
    if (argc >= 2) cfg.input_file  = argv[1];
    if (argc >= 3) cfg.output_file = argv[2];

    // ── IO ───────────────────────────────────────────────────────────────────
    cfg.log_file      = "pw-sim.log.csv";

    // ── Engine ───────────────────────────────────────────────────────────────
    cfg.chunk_size    = 256;      // frames per FUT call (try 64, 128, 512, 1024)
    cfg.sample_rate   = 48000;    // Hz — must match input file for correct deadline
    cfg.warmup_chunks = 4;

    // ── Clock mode ───────────────────────────────────────────────────────────
    // SEQUENTIAL : overruns logged only, FUT output always written
    // REALTIME   : overruns trigger xrun_policy, virtual clock tracks debt
    // cfg.clock_mode = ClockMode::SEQUENTIAL;
    cfg.clock_mode = ClockMode::REALTIME;

    // ── Pre-fill ─────────────────────────────────────────────────────────────
    // ZEROS       : realistic (PipeWire pre-zeros the output buffer)
    // PASSTHROUGH : simulate a plugin that did memcpy(out,in) as safety net
    cfg.pre_fill = PreFillPolicy::ZEROS;
    // cfg.pre_fill = PreFillPolicy::PASSTHROUGH;

    // ── REALTIME mode options ─────────────────────────────────────────────────
    cfg.xrun_policy = XrunPolicy::ZEROS;          // silence (PipeWire default)
    // cfg.xrun_policy = XrunPolicy::REPEAT_LAST; // repeat last good chunk
    // cfg.xrun_policy = XrunPolicy::PASSTHROUGH; // dry audio for that chunk

    // Subtract from budget to model driver/scheduling overhead:
    cfg.deadline_offset_us = 0.0;   // e.g. 200.0 for 200us overhead model

    // ── Probes ────────────────────────────────────────────────────────────────
    cfg.probe_cpu_time          = true;
    cfg.probe_context_switches  = true;
    cfg.probe_page_faults       = true;

    // ── Startup checks ────────────────────────────────────────────────────────
    cfg.warn_cpu_governor = true;
    cfg.try_rt_priority   = false;  // true = attempt SCHED_FIFO (needs root)

    return cfg;
}

static FutFn make_active_fut(bool use_rnnoise, const std::string& nam_model) {

    // ── RNNoise (xiph/rnnoise) noise suppression ──────────────────────────────
    // Enabled via: ./pw-sim --rnnoise [input.wav] [output.wav]
    if (use_rnnoise) {
        std::fprintf(stderr, "[pw-sim] FUT: RNNoise (480-sample frames, ±32768 scale)\n");
        return make_rnnoise_fut();
    }

    // ── NAM (Neural Amp Modeler) via NeuralAmpModelerCore ─────────────────────
    // Enabled via: ./pw-sim --nam model.nam [input.wav] [output.wav]
    if (!nam_model.empty())
        return make_nam_fut(nam_model);

    // ── Option 1: Passthrough ─────────────────────────────────────────────────
    // Output == input. Verifies the simulator pipeline is correct end-to-end.
    //return make_passthrough_fut();

    // ── Option 2: Gain ────────────────────────────────────────────────────────
    // return make_gain_fut(/*gain=*/0.5f);

    // ── Option 3: Slow — triggers overruns deliberately ───────────────────────
    // delay_us=8000, budget=5333us → every chunk overruns (ratio ~1.5x)
    // slow_every_n=20 → only every 20th chunk overruns (~5% rate)
    // return make_slow_fut(/*delay_us=*/8000, /*slow_every_n=*/0);
    // return make_slow_fut(/*delay_us=*/8000, /*slow_every_n=*/20);

    // ── Option 4: Your FUT ───────────────────────────────────────────────────
     return [](const float* input, float* output,
               size_t frames, size_t channels, size_t chunk_index)
     {
         (void)chunk_index;
         for (size_t i = 0; i < frames * channels; ++i)
             output[i] = input[i]*0.2;  // replace with your model
    };
}

int main(int argc, char* argv[]) {
    try {
        bool        use_rnnoise = consume_flag(argc, argv, "--rnnoise");
        std::string nam_model   = consume_value_flag(argc, argv, "--nam");

        if (use_rnnoise && !nam_model.empty())
            throw std::runtime_error("--rnnoise and --nam are mutually exclusive");

        SimConfig cfg = make_config(argc, argv);
        FutFn     fut = make_active_fut(use_rnnoise, nam_model);
        SimEngine engine(cfg, std::move(fut));
        engine.run();
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "\npw-sim error: %s\n\n", e.what());
        return EXIT_FAILURE;
    }
}
