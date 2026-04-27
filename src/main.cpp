#include "config/SimConfig.hpp"
#include "config/TomlLoader.hpp"
#include "config/CliParser.hpp"
#include "engine/SimEngine.hpp"
#include "fut/FutInterface.hpp"
#include "fut/stubs/PassthroughFut.hpp"
#include "fut/stubs/GainFut.hpp"
#include "fut/stubs/SlowFut.hpp"
#include "fut/RnnoiseFut.hpp"
#include "fut/NamFut.hpp"
#ifdef PW_SIM_HAVE_SPLEETER
#include "fut/SpleeterFut.hpp"
#endif
#ifdef PW_SIM_HAVE_RUBBERBAND
#include "fut/RubberbandFut.hpp"
#endif
#if defined(PW_SIM_HAVE_SPLEETER) && defined(PW_SIM_HAVE_RUBBERBAND)
#include "fut/SpleeterRubberbandFut.hpp"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

// Strip a boolean flag from argv in-place; returns true if found.
static bool consume_flag(int& argc, char* argv[], const char* flag) {
    bool found = false;
    int j = 0;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0) { found = true; }
        else                                  { argv[j++] = argv[i]; }
    }
    argc = j;
    return found;
}

// Strip a value flag (--flag VALUE) from argv; returns the value, or "" if absent.
static std::string consume_value_flag(int& argc, char* argv[], const char* flag) {
    std::string value;
    int j = 0;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0 && i + 1 < argc)
            value = argv[++i];
        else
            argv[j++] = argv[i];
    }
    argc = j;
    return value;
}

static void print_usage() {
    std::printf(
        "Usage: pw-sim [--config <file>] [flags...] [--rnnoise | --nam <model.nam>]\n"
        "              [input.wav [output.wav]]\n"
        "\n"
        "Flags (override pw-sim.toml):\n"
        "  --config <file>                  load TOML config (default: built-in defaults)\n"
        "  --input <file>                   input audio file\n"
        "  --output <file>                  output WAV file\n"
        "  --log-file <file>                per-chunk CSV log\n"
        "  --clock-mode <sequential|realtime>\n"
        "  --chunk-size <N>                 frames per FUT call (64, 128, 256, 512, 1024)\n"
        "  --sample-rate <N>                Hz — must match input file\n"
        "  --warmup-chunks <N>              silent warmup chunks before metrics start\n"
        "  --pre-fill <zeros|passthrough>   output buffer state before FUT call\n"
        "  --xrun-policy <zeros|repeat_last|passthrough>\n"
        "  --tail-policy <zero_pad|drop>    last partial chunk handling\n"
        "  --output-format <same|s16|s24|f32>\n"
        "  --deadline-offset <µs>           subtract from budget (simulate overhead)\n"
        "  --probe-cpu <on|off>\n"
        "  --probe-ctx <on|off>\n"
        "  --probe-faults <on|off>\n"
        "\n"
        "FUT selection (mutually exclusive):\n"
        "  --rnnoise                        use RNNoise noise suppression\n"
        "  --nam <model.nam>                use Neural Amp Modeler model\n"
#ifdef PW_SIM_HAVE_SPLEETER
        "  --spleeter <model_dir>           use Spleeter 2-stem source separation\n"
        "  --spleeter-stem <vocals|accompaniment>  which stem to output (default: vocals)\n"
#else
        "  --spleeter                       [disabled — libtensorflow not found at build time]\n"
#endif
#ifdef PW_SIM_HAVE_RUBBERBAND
        "  --rubberband                     use Rubberband real-time pitch shifting\n"
        "  --rubberband-semitones <N>       semitones to shift (float, default: 2.0)\n"
#else
        "  --rubberband                     [disabled — librubberband not found at build time]\n"
#endif
        "\n"
        "See configure.md for the full TOML schema and example runs.\n"
    );
}

static FutFn make_active_fut(bool use_rnnoise, const std::string& nam_model,
                             const std::string& spleeter_model,
                             const std::string& spleeter_stem,
                             bool use_rubberband, double rubberband_semitones,
                             size_t sample_rate) {

    // ── RNNoise ───────────────────────────────────────────────────────────────
    if (use_rnnoise) {
        std::fprintf(stderr, "[pw-sim] FUT: RNNoise (480-sample frames, ±32768 scale)\n");
        return make_rnnoise_fut();
    }

    // ── NAM ───────────────────────────────────────────────────────────────────
    if (!nam_model.empty())
        return make_nam_fut(nam_model);

    // ── Spleeter + Rubberband combined (pitch-shift vocals, pass accompaniment through) ──
#if defined(PW_SIM_HAVE_SPLEETER) && defined(PW_SIM_HAVE_RUBBERBAND)
    if (!spleeter_model.empty() && use_rubberband)
        return make_spleeter_rubberband_fut(spleeter_model, rubberband_semitones, sample_rate,
                                            spleeter_stem == "accompaniment");
#endif

    // ── Spleeter alone ────────────────────────────────────────────────────────
#ifdef PW_SIM_HAVE_SPLEETER
    if (!spleeter_model.empty())
        return make_spleeter_fut(spleeter_model, spleeter_stem);
#else
    (void)spleeter_model;
    (void)spleeter_stem;
#endif

    // ── Rubberband alone ──────────────────────────────────────────────────────
#ifdef PW_SIM_HAVE_RUBBERBAND
    if (use_rubberband)
        return make_rubberband_fut(sample_rate, rubberband_semitones);
#else
    (void)use_rubberband;
    (void)rubberband_semitones;
    (void)sample_rate;
#endif

    // ── Option 1: Passthrough ─────────────────────────────────────────────────
    // return make_passthrough_fut();

    // ── Option 2: Gain ────────────────────────────────────────────────────────
    // return make_gain_fut(/*gain=*/0.5f);

    // ── Option 3: Slow — deliberate overruns ─────────────────────────────────
    // return make_slow_fut(/*delay_us=*/8000, /*slow_every_n=*/0);
    // return make_slow_fut(/*delay_us=*/8000, /*slow_every_n=*/20);

    // ── Option 4: Your FUT ───────────────────────────────────────────────────
    return [](const float* input, float* output,
              size_t frames, size_t channels, size_t chunk_index) {
        (void)chunk_index;
        for (size_t i = 0; i < frames * channels; ++i)
            output[i] = input[i] * 0.2f;
    };
}

int main(int argc, char* argv[]) {
    try {
        if (consume_flag(argc, argv, "--help") || consume_flag(argc, argv, "-h")) {
            print_usage();
            return EXIT_SUCCESS;
        }

        // 1. Extract FUT flags before any config parsing.
        bool        use_rnnoise          = consume_flag(argc, argv, "--rnnoise");
        std::string nam_model            = consume_value_flag(argc, argv, "--nam");
        std::string spleeter_model       = consume_value_flag(argc, argv, "--spleeter");
        std::string spleeter_stem        = consume_value_flag(argc, argv, "--spleeter-stem");
        bool        use_rubberband       = consume_flag(argc, argv, "--rubberband");
        std::string rubberband_semi_str  = consume_value_flag(argc, argv, "--rubberband-semitones");
        if (spleeter_stem.empty()) spleeter_stem = "vocals";
        double rubberband_semitones = rubberband_semi_str.empty()
                                      ? 2.0
                                      : std::stod(rubberband_semi_str);

        {
            // spleeter and rubberband may be combined; treat the pair as one slot
            bool spleeter_group = !spleeter_model.empty() || use_rubberband;
            int fut_count = (int)use_rnnoise + (int)!nam_model.empty() + (int)spleeter_group;
            if (fut_count > 1)
                throw std::runtime_error("--rnnoise, --nam, and --spleeter/--rubberband are mutually exclusive");
        }

#ifndef PW_SIM_HAVE_SPLEETER
        if (!spleeter_model.empty())
            throw std::runtime_error("--spleeter: libtensorflow was not found at build time — rebuild with libtensorflow installed");
#endif
#ifndef PW_SIM_HAVE_RUBBERBAND
        if (use_rubberband)
            throw std::runtime_error("--rubberband: librubberband was not found at build time — install rubberband-dev and rebuild");
#endif

        // 2. Parse all config flags, stripping them from argv.
        config::CliArgs cli = config::parse_cli(argc, argv);

        // 3. Load TOML base config (if --config given), else use defaults.
        SimConfig cfg = cli.config_file
            ? config::load_toml(*cli.config_file)
            : SimConfig{};

        // 4. Apply CLI overrides on top of TOML (or defaults).
        cli.apply_to(cfg);

        // 5. Positional args — backward compat: ./pw-sim [input.wav] [output.wav]
        //    Ignored if --input / --output were already given.
        if (argc >= 2 && !cli.input_file)  cfg.input_file  = argv[1];
        if (argc >= 3 && !cli.output_file) cfg.output_file = argv[2];

        // 6. Run.
        FutFn     fut = make_active_fut(use_rnnoise, nam_model, spleeter_model, spleeter_stem,
                                        use_rubberband, rubberband_semitones, cfg.sample_rate);
        SimEngine engine(cfg, std::move(fut));
        engine.run();
        return EXIT_SUCCESS;

    } catch (const std::exception& e) {
        std::fprintf(stderr, "\npw-sim error: %s\n\n", e.what());
        return EXIT_FAILURE;
    }
}
