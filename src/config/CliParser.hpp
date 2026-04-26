#pragma once
#include "config/SimConfig.hpp"
#include <optional>
#include <string>

namespace config {

struct CliArgs {
    std::optional<std::string>   config_file;

    // Per-field overrides — populated only when the flag was present.
    std::optional<std::string>   input_file;
    std::optional<std::string>   output_file;
    std::optional<std::string>   log_file;
    std::optional<ClockMode>     clock_mode;
    std::optional<size_t>        chunk_size;
    std::optional<size_t>        sample_rate;
    std::optional<size_t>        warmup_chunks;
    std::optional<PreFillPolicy> pre_fill;
    std::optional<XrunPolicy>    xrun_policy;
    std::optional<TailPolicy>    tail_policy;
    std::optional<std::string>   output_format;
    std::optional<double>        deadline_budget_us_offset;
    std::optional<bool>          probe_cpu_time;
    std::optional<bool>          probe_context_switches;
    std::optional<bool>          probe_page_faults;

    // Merge populated overrides into cfg.
    void apply_to(SimConfig& cfg) const;
};

// Parse pw-sim flags from argv, removing them in-place.
// Call AFTER consuming --rnnoise / --nam so those are already stripped.
// Throws std::invalid_argument on bad enum values or malformed numbers.
CliArgs parse_cli(int& argc, char* argv[]);

} // namespace config
