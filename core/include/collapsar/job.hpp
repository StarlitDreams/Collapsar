#pragma once

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

#include "collapsar/error.hpp"
#include "collapsar/options.hpp"
#include "collapsar/progress.hpp"

namespace collapsar {

struct JobRequest {
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path              output;
    JobOptions                         options{};
    ProgressCallback                   on_progress{};
};

struct JobStats {
    std::size_t                files_processed   = 0;
    std::size_t                files_skipped     = 0;
    std::size_t                input_bytes       = 0;
    std::size_t                output_bytes      = 0;
    std::size_t                cpu_blocks        = 0;
    std::size_t                gpu_blocks        = 0;
    std::chrono::milliseconds  elapsed{0};
};

class JobHandleImpl;  // PIMPL — opaque to callers and to the C++/CLI bridge.

class JobHandle {
public:
    explicit JobHandle(std::shared_ptr<JobHandleImpl> impl);
    ~JobHandle();

    JobHandle(const JobHandle&) = delete;
    JobHandle& operator=(const JobHandle&) = delete;
    JobHandle(JobHandle&&) noexcept;
    JobHandle& operator=(JobHandle&&) noexcept;

    // Block until the job finishes, fails, or is cancelled.
    void wait();

    // Request cancellation. Idempotent; returns immediately. Use wait() to
    // observe completion.
    void cancel();

    // Non-blocking peek at the most recent progress event. Returns the empty
    // event if no progress has been published yet.
    [[nodiscard]] ProgressEvent progress() const;

    [[nodiscard]] bool      done()  const noexcept;
    [[nodiscard]] Error     error() const;
    [[nodiscard]] JobStats  stats() const;

private:
    std::shared_ptr<JobHandleImpl> impl_;
};

} // namespace collapsar
