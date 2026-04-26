#pragma once
#include <cstddef>
#include <string>

// =============================================================================
// pw-sim — Configuration (Tier 3)
//
// Populated from pw-sim.toml + CLI overrides. See configure.md for the full
// TOML schema and CLI flag reference.
// =============================================================================

enum class ClockMode    { SEQUENTIAL, REALTIME };
enum class XrunPolicy   { ZEROS, REPEAT_LAST, PASSTHROUGH };
enum class PreFillPolicy { ZEROS, PASSTHROUGH };
enum class TailPolicy   { ZERO_PAD, DROP };

struct SimConfig {

    // ── IO ───────────────────────────────────────────────────────────────────
    std::string input_file  = "input.wav";
    std::string output_file = "output.wav";
    std::string log_file    = "pw-sim.log.csv";

    // Output sample encoding: "same" | "s16" | "s24" | "f32"
    // "same" mirrors the input file's bit depth. Others convert on write.
    std::string output_format = "same";

    // ── Engine ───────────────────────────────────────────────────────────────
    ClockMode     clock_mode    = ClockMode::REALTIME;
    size_t        chunk_size    = 256;
    size_t        sample_rate   = 48000;
    size_t        warmup_chunks = 4;
    PreFillPolicy pre_fill      = PreFillPolicy::ZEROS;

    // Last partial chunk handling: ZERO_PAD (process it) | DROP (skip it).
    TailPolicy tail_policy = TailPolicy::ZERO_PAD;

    // ── REALTIME mode ────────────────────────────────────────────────────────
    XrunPolicy xrun_policy              = XrunPolicy::ZEROS;
    double     deadline_budget_us_offset = 0.0;

    // ── Probes ───────────────────────────────────────────────────────────────
    bool probe_cpu_time         = true;
    bool probe_context_switches = true;
    bool probe_page_faults      = true;

    // ── Startup checks ───────────────────────────────────────────────────────
    bool warn_cpu_governor = true;
    bool try_rt_priority   = false;
};

// Effective deadline in µs (period − deadline_budget_us_offset).
inline double effective_deadline_us(const SimConfig& cfg) {
    double raw = (static_cast<double>(cfg.chunk_size) /
                  static_cast<double>(cfg.sample_rate)) * 1e6;
    return raw - cfg.deadline_budget_us_offset;
}

inline double deadline_us(const SimConfig& cfg) {
    return effective_deadline_us(cfg);
}

// String → enum converters (defined in SimConfig.cpp).
// Throw std::invalid_argument on unrecognised strings.
ClockMode     clock_mode_from_string(const std::string& s);
XrunPolicy    xrun_policy_from_string(const std::string& s);
PreFillPolicy prefill_from_string(const std::string& s);
TailPolicy    tail_policy_from_string(const std::string& s);

// Enum → string (for display / logging).
const char* clock_mode_str(ClockMode m);
const char* xrun_policy_str(XrunPolicy p);
const char* prefill_str(PreFillPolicy p);
const char* tail_policy_str(TailPolicy p);
