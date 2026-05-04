#pragma once

#include <memory>

#include "collapsar/capabilities.hpp"
#include "collapsar/job.hpp"
#include "collapsar/options.hpp"
#include "collapsar/result.hpp"

namespace collapsar {

// Engine is the top-level facade: it owns the thread pools, CUDA streams, and
// pinned host staging buffers. One Engine per process is the expected pattern,
// but multiple instances with disjoint resources are supported.
class Engine {
public:
    explicit Engine(EngineOptions options = {});
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;
    Engine(Engine&&) noexcept;
    Engine& operator=(Engine&&) noexcept;

    // Submit a job for asynchronous execution. The returned handle keeps the
    // job alive until destroyed; dropping it implicitly cancels.
    [[nodiscard]] Result<JobHandle> submit(JobRequest request);

    // Synchronous convenience wrapper. Submits and waits.
    [[nodiscard]] Result<JobStats>  run(JobRequest request);

    [[nodiscard]] const EngineOptions& options() const noexcept;
    [[nodiscard]] const Capabilities&  capabilities() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace collapsar
