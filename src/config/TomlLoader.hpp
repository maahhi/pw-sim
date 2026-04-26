#pragma once
#include "config/SimConfig.hpp"
#include <string>

namespace config {

// Load pw-sim.toml from `path` and return a fully-populated SimConfig.
// Keys absent from the file keep SimConfig's defaults.
// Throws std::runtime_error on file-not-found or TOML parse error.
// Throws std::invalid_argument on bad enum values.
SimConfig load_toml(const std::string& path);

} // namespace config
