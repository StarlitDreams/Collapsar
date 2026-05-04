#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace collapsar {

// A single progress notification. Coalesced by the engine to fire at most a
// few times per second so GUI dispatchers don't get swamped.
struct ProgressEvent {
    std::size_t           total_files       = 0;
    std::size_t           total_bytes       = 0;
    std::size_t           processed_files   = 0;
    std::size_t           processed_bytes   = 0;
    std::size_t           output_bytes      = 0;
    std::filesystem::path current_file{};
    double                throughput_mibps  = 0.0;
};

// Sink interface so callers can either pass a lambda or implement a class.
using ProgressCallback = std::function<void(const ProgressEvent&)>;

} // namespace collapsar
