#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <thread>

#include "collapsar/format.hpp"

namespace collapsar {

// EngineOptions configures global resources owned by an Engine instance.
// These can't change for the lifetime of the engine.
struct EngineOptions {
    std::size_t io_threads             = 4;
    std::size_t cpu_codec_threads      = std::thread::hardware_concurrency();
    bool        enable_gpu             = true;
    int         cuda_device            = 0;
    std::size_t gpu_streams            = 3;        // triple-buffered H2D/compute/D2H
    std::size_t gpu_memory_budget_mib  = 1024;     // max device-side staging
};

// JobOptions configures one compression operation.
struct JobOptions {
    Format    format               = Format::Zip;
    Algorithm algorithm            = Algorithm::Auto;
    Level     level                = Level::Balanced;

    // Files smaller than this stay on the CPU path.
    std::size_t gpu_threshold_bytes = 1ull * 1024 * 1024;       // 1 MiB

    // Accumulate up to this many bytes per nvCOMP batched call.
    std::size_t gpu_batch_bytes     = 256ull * 1024 * 1024;     // 256 MiB

    // Soft cap on per-block payload size for very large files.
    std::size_t block_bytes         = 8ull * 1024 * 1024;       // 8 MiB

    // If true, recurse into directories given as inputs.
    bool        recurse             = true;

    // If true and the output already exists, overwrite it.
    bool        overwrite           = false;
};

} // namespace collapsar
