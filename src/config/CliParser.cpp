#include "config/CliParser.hpp"
#include <cstring>
#include <stdexcept>
#include <string>

namespace config {

static bool parse_bool(const std::string& val, const char* flag) {
    if (val == "on"  || val == "true"  || val == "1") return true;
    if (val == "off" || val == "false" || val == "0") return false;
    throw std::invalid_argument(
        std::string(flag) + ": expected on/off, got '" + val + "'");
}

// Strip --flag VALUE pair from argv in-place; returns value or nullopt.
static std::optional<std::string> consume_kv(int& argc, char* argv[],
                                             const char* flag) {
    std::optional<std::string> result;
    int j = 0;
    for (int i = 0; i < argc; ++i) {
        if (std::strcmp(argv[i], flag) == 0 && i + 1 < argc)
            result = argv[++i];
        else
            argv[j++] = argv[i];
    }
    argc = j;
    return result;
}

void CliArgs::apply_to(SimConfig& cfg) const {
    if (input_file)                cfg.input_file                = *input_file;
    if (output_file)               cfg.output_file               = *output_file;
    if (log_file)                  cfg.log_file                  = *log_file;
    if (clock_mode)                cfg.clock_mode                = *clock_mode;
    if (chunk_size)                cfg.chunk_size                = *chunk_size;
    if (sample_rate)               cfg.sample_rate               = *sample_rate;
    if (warmup_chunks)             cfg.warmup_chunks             = *warmup_chunks;
    if (pre_fill)                  cfg.pre_fill                  = *pre_fill;
    if (xrun_policy)               cfg.xrun_policy               = *xrun_policy;
    if (tail_policy)               cfg.tail_policy               = *tail_policy;
    if (output_format)             cfg.output_format             = *output_format;
    if (deadline_budget_us_offset) cfg.deadline_budget_us_offset = *deadline_budget_us_offset;
    if (probe_cpu_time)            cfg.probe_cpu_time            = *probe_cpu_time;
    if (probe_context_switches)    cfg.probe_context_switches    = *probe_context_switches;
    if (probe_page_faults)         cfg.probe_page_faults         = *probe_page_faults;
}

CliArgs parse_cli(int& argc, char* argv[]) {
    CliArgs args;

    args.config_file = consume_kv(argc, argv, "--config");
    args.input_file  = consume_kv(argc, argv, "--input");
    args.output_file = consume_kv(argc, argv, "--output");
    args.log_file    = consume_kv(argc, argv, "--log-file");

    if (auto v = consume_kv(argc, argv, "--clock-mode"))
        args.clock_mode = clock_mode_from_string(*v);

    if (auto v = consume_kv(argc, argv, "--chunk-size")) {
        try { args.chunk_size = static_cast<size_t>(std::stoul(*v)); }
        catch (...) {
            throw std::invalid_argument("--chunk-size: not a valid integer: " + *v);
        }
    }

    if (auto v = consume_kv(argc, argv, "--sample-rate")) {
        try { args.sample_rate = static_cast<size_t>(std::stoul(*v)); }
        catch (...) {
            throw std::invalid_argument("--sample-rate: not a valid integer: " + *v);
        }
    }

    if (auto v = consume_kv(argc, argv, "--warmup-chunks")) {
        try { args.warmup_chunks = static_cast<size_t>(std::stoul(*v)); }
        catch (...) {
            throw std::invalid_argument("--warmup-chunks: not a valid integer: " + *v);
        }
    }

    if (auto v = consume_kv(argc, argv, "--pre-fill"))
        args.pre_fill = prefill_from_string(*v);

    if (auto v = consume_kv(argc, argv, "--xrun-policy"))
        args.xrun_policy = xrun_policy_from_string(*v);

    if (auto v = consume_kv(argc, argv, "--tail-policy"))
        args.tail_policy = tail_policy_from_string(*v);

    if (auto v = consume_kv(argc, argv, "--output-format"))
        args.output_format = *v;

    if (auto v = consume_kv(argc, argv, "--deadline-offset")) {
        try { args.deadline_budget_us_offset = std::stod(*v); }
        catch (...) {
            throw std::invalid_argument("--deadline-offset: not a valid number: " + *v);
        }
    }

    if (auto v = consume_kv(argc, argv, "--probe-cpu"))
        args.probe_cpu_time = parse_bool(*v, "--probe-cpu");

    if (auto v = consume_kv(argc, argv, "--probe-ctx"))
        args.probe_context_switches = parse_bool(*v, "--probe-ctx");

    if (auto v = consume_kv(argc, argv, "--probe-faults"))
        args.probe_page_faults = parse_bool(*v, "--probe-faults");

    return args;
}

} // namespace config
