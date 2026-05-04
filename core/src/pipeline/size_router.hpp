#pragma once

#include <cstddef>
#include <cstdint>

#include "collapsar/options.hpp"

namespace collapsar {

// SizeRouter is a stateless rule that says "should this file go to the GPU?".
// Pulled out into its own type so we can unit-test the policy independently
// of pipeline plumbing — and so future heuristics (per-format, per-extension,
// per-entropy) can plug in without touching the pipeline code.
class SizeRouter {
public:
    SizeRouter(const JobOptions& options, bool gpu_available) noexcept
        : threshold_{options.gpu_threshold_bytes},
          algorithm_{options.algorithm},
          gpu_available_{gpu_available} {}

    enum class Decision { Cpu, Gpu };

    [[nodiscard]] Decision route(std::uint64_t file_size_bytes) const noexcept {
        if (!gpu_available_)                        return Decision::Cpu;
        if (algorithm_ == Algorithm::CpuDeflate)    return Decision::Cpu;
        if (algorithm_ == Algorithm::GpuDeflate ||
            algorithm_ == Algorithm::GpuGDeflate)   return Decision::Gpu;

        // Algorithm::Auto — gate on size.
        return file_size_bytes >= threshold_ ? Decision::Gpu : Decision::Cpu;
    }

private:
    std::size_t threshold_;
    Algorithm   algorithm_;
    bool        gpu_available_;
};

} // namespace collapsar
