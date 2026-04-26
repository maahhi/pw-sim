#include "config/TomlLoader.hpp"
#include <toml++/toml.hpp>
#include <stdexcept>
#include <string>

namespace config {

SimConfig load_toml(const std::string& path) {
    SimConfig cfg;

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error& e) {
        throw std::runtime_error(
            std::string("TOML parse error in '") + path + "': " + e.what());
    }

    // [engine]
    if (auto* t = tbl["engine"].as_table()) {
        if (auto v = (*t)["clock_mode"].value<std::string>())
            cfg.clock_mode = clock_mode_from_string(*v);
        if (auto v = (*t)["chunk_size"].value<int64_t>())
            cfg.chunk_size = static_cast<size_t>(*v);
        if (auto v = (*t)["sample_rate"].value<int64_t>())
            cfg.sample_rate = static_cast<size_t>(*v);
        if (auto v = (*t)["warmup_chunks"].value<int64_t>())
            cfg.warmup_chunks = static_cast<size_t>(*v);
        if (auto v = (*t)["pre_fill_output"].value<std::string>())
            cfg.pre_fill = prefill_from_string(*v);
    }

    // [io]
    if (auto* t = tbl["io"].as_table()) {
        if (auto v = (*t)["input_file"].value<std::string>())
            cfg.input_file = *v;
        if (auto v = (*t)["output_file"].value<std::string>())
            cfg.output_file = *v;
        if (auto v = (*t)["output_format"].value<std::string>())
            cfg.output_format = *v;
        if (auto v = (*t)["tail_policy"].value<std::string>())
            cfg.tail_policy = tail_policy_from_string(*v);
    }

    // [realtime]
    if (auto* t = tbl["realtime"].as_table()) {
        if (auto v = (*t)["xrun_policy"].value<std::string>())
            cfg.xrun_policy = xrun_policy_from_string(*v);
        // Accept both integer (0) and float (0.0) — TOML writers vary.
        if (auto v = (*t)["deadline_budget_us_offset"].value<double>())
            cfg.deadline_budget_us_offset = *v;
        else if (auto vi = (*t)["deadline_budget_us_offset"].value<int64_t>())
            cfg.deadline_budget_us_offset = static_cast<double>(*vi);
    }

    // [metrics]
    if (auto* t = tbl["metrics"].as_table()) {
        if (auto v = (*t)["log_file"].value<std::string>())
            cfg.log_file = *v;
        if (auto v = (*t)["warn_cpu_governor"].value<bool>())
            cfg.warn_cpu_governor = *v;
        if (auto v = (*t)["try_rt_priority"].value<bool>())
            cfg.try_rt_priority = *v;
    }

    // [probes]
    if (auto* t = tbl["probes"].as_table()) {
        if (auto v = (*t)["cpu_time"].value<bool>())
            cfg.probe_cpu_time = *v;
        if (auto v = (*t)["context_switches"].value<bool>())
            cfg.probe_context_switches = *v;
        if (auto v = (*t)["page_faults"].value<bool>())
            cfg.probe_page_faults = *v;
    }

    return cfg;
}

} // namespace config
