#include "collapsar/engine.hpp"

#include <algorithm>
#include <future>
#include <memory>
#include <utility>

#include "job_handle_impl.hpp"
#include "pipeline/pipeline.hpp"
#include "util/log.hpp"
#include "util/thread_pool.hpp"

namespace collapsar {

struct Engine::Impl {
    EngineOptions               options;
    Capabilities                capabilities;
    std::unique_ptr<ThreadPool> cpu_pool;
    std::unique_ptr<ThreadPool> io_pool;
    Pipeline                    pipeline;

    Impl(EngineOptions opts)
        : options{std::move(opts)},
          capabilities{probe_capabilities()},
          cpu_pool{std::make_unique<ThreadPool>(std::max<std::size_t>(options.cpu_codec_threads, 1))},
          io_pool{std::make_unique<ThreadPool>(std::max<std::size_t>(options.io_threads, 1))},
          pipeline{options, *cpu_pool, *io_pool}
    {}
};

Engine::Engine(EngineOptions options) : impl_{std::make_unique<Impl>(std::move(options))} {
    COLLAPSAR_LOG_INFO()
        << "Collapsar engine started — cuda=" << (impl_->capabilities.has_cuda ? "yes" : "no")
        << " devices=" << impl_->capabilities.cuda_device_count
        << " cpu_threads=" << impl_->cpu_pool->size()
        << " io_threads=" << impl_->io_pool->size();
}

Engine::~Engine() = default;
Engine::Engine(Engine&&) noexcept            = default;
Engine& Engine::operator=(Engine&&) noexcept = default;

Result<JobHandle> Engine::submit(JobRequest request) {
    if (request.inputs.empty()) {
        return make_error(StatusCode::InvalidArgument, "JobRequest::inputs is empty");
    }
    if (request.output.empty()) {
        return make_error(StatusCode::InvalidArgument, "JobRequest::output is empty");
    }

    auto handle_state = std::make_shared<JobHandleImpl>();
    auto stop         = handle_state->get_stop_token();

    // The pipeline's progress callback writes back into JobHandleImpl so
    // callers can poll progress() between wait() calls.
    auto progress_cb = [handle_state, user_cb = std::move(request.on_progress)](const ProgressEvent& e) {
        handle_state->publish_progress(e);
        if (user_cb) user_cb(e);
    };
    request.on_progress = nullptr;

    // Run the pipeline on its own thread so submit() returns immediately.
    auto future = std::async(std::launch::async,
        [this, req = std::move(request), stop, cb = std::move(progress_cb)]() mutable -> Result<JobStats> {
            return impl_->pipeline.run(std::move(req), stop, std::move(cb));
        });

    handle_state->set_future(std::move(future));
    return JobHandle{std::move(handle_state)};
}

Result<JobStats> Engine::run(JobRequest request) {
    auto handle_res = submit(std::move(request));
    if (!handle_res) return std::move(handle_res).error();
    JobHandle h = std::move(handle_res).value();
    h.wait();
    if (auto err = h.error(); !err.ok()) return err;
    return h.stats();
}

const EngineOptions& Engine::options()      const noexcept { return impl_->options; }
const Capabilities&  Engine::capabilities() const noexcept { return impl_->capabilities; }

} // namespace collapsar
