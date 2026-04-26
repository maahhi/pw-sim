#include "config/SimConfig.hpp"
#include <stdexcept>
#include <string>

ClockMode clock_mode_from_string(const std::string& s) {
    if (s == "sequential") return ClockMode::SEQUENTIAL;
    if (s == "realtime")   return ClockMode::REALTIME;
    throw std::invalid_argument(
        "unknown clock_mode '" + s + "' (valid: sequential, realtime)");
}

XrunPolicy xrun_policy_from_string(const std::string& s) {
    if (s == "zeros")       return XrunPolicy::ZEROS;
    if (s == "repeat_last") return XrunPolicy::REPEAT_LAST;
    if (s == "passthrough") return XrunPolicy::PASSTHROUGH;
    throw std::invalid_argument(
        "unknown xrun_policy '" + s + "' (valid: zeros, repeat_last, passthrough)");
}

PreFillPolicy prefill_from_string(const std::string& s) {
    if (s == "zeros")       return PreFillPolicy::ZEROS;
    if (s == "passthrough") return PreFillPolicy::PASSTHROUGH;
    throw std::invalid_argument(
        "unknown pre_fill_output '" + s + "' (valid: zeros, passthrough)");
}

TailPolicy tail_policy_from_string(const std::string& s) {
    if (s == "zero_pad") return TailPolicy::ZERO_PAD;
    if (s == "drop")     return TailPolicy::DROP;
    throw std::invalid_argument(
        "unknown tail_policy '" + s + "' (valid: zero_pad, drop)");
}

const char* clock_mode_str(ClockMode m) {
    return m == ClockMode::REALTIME ? "REALTIME" : "SEQUENTIAL";
}

const char* xrun_policy_str(XrunPolicy p) {
    switch (p) {
        case XrunPolicy::ZEROS:       return "zeros";
        case XrunPolicy::REPEAT_LAST: return "repeat_last";
        case XrunPolicy::PASSTHROUGH: return "passthrough";
    }
    return "unknown";
}

const char* prefill_str(PreFillPolicy p) {
    return p == PreFillPolicy::PASSTHROUGH ? "passthrough" : "zeros";
}

const char* tail_policy_str(TailPolicy p) {
    return p == TailPolicy::DROP ? "drop" : "zero_pad";
}
