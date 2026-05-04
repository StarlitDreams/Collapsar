#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <stop_token>

#include "collapsar/job.hpp"
#include "collapsar/options.hpp"
#include "collapsar/progress.hpp"
#include "collapsar/result.hpp"
#include "../util/thread_pool.hpp"

namespace collapsar {

// Pipeline runs one JobRequest end-to-end. run() blocks until the job
// finishes and returns the final stats (or an error). Cancellation is observed
// at every queue-pop and on each block boundary.
//
// `on_event` is invoked from the assembler thread, coalesced internally to
// fire at most a few times per second.
class Pipeline {
public:
    Pipeline(EngineOptions engine,
             ThreadPool&   cpu_pool,
             ThreadPool&   io_pool);

    [[nodiscard]] Result<JobStats> run(JobRequest       request,
                                        std::stop_token  stop,
                                        ProgressCallback on_event);

private:
    EngineOptions engine_;
    ThreadPool&   cpu_pool_;
    ThreadPool&   io_pool_;
};

} // namespace collapsar
