// Implementation of the C ABI bridge declared in c_bridge.h. Translates flat
// C structs into the native engine's C++ types and back. Strings are copied
// across the boundary so the engine and managed callers can free their
// allocations independently.

#define COLLAPSAR_BRIDGE_EXPORTS 1
#include "c_bridge.h"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "collapsar/engine.hpp"

namespace {

char* dup_cstring(const std::string& s) {
    char* out = static_cast<char*>(std::malloc(s.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, s.data(), s.size());
    out[s.size()] = '\0';
    return out;
}

collapsar::Format     to_native_format(int32_t v)    { return static_cast<collapsar::Format>(v); }
collapsar::Algorithm  to_native_algorithm(int32_t v) { return static_cast<collapsar::Algorithm>(v); }
collapsar::Level      to_native_level(int32_t v)     { return static_cast<collapsar::Level>(v); }

}  // namespace

// Opaque handle holding the native engine plus a pointer to the in-flight job
// so cancel() can reach it from another thread.
struct collapsar_engine_handle {
    std::unique_ptr<collapsar::Engine> engine;

    // Currently-running job. Protected by mutex so cancel() races safely with
    // pack() finishing.
    std::mutex                         job_mutex;
    std::shared_ptr<collapsar::JobHandle> job;
};

extern "C" {

// ---- Capabilities -----------------------------------------------------------

void COLLAPSAR_BRIDGE_CALL
collapsar_probe_capabilities(collapsar_capabilities_t* out) {
    if (!out) return;
    *out = {};

    auto caps = collapsar::probe_capabilities();
    out->has_cuda          = caps.has_cuda ? 1 : 0;
    out->cuda_device_count = caps.cuda_device_count;

    if (caps.cuda_device_names.empty()) return;

    // Concatenate device names with embedded NULs so the managed side can
    // walk them with a single allocation.
    std::size_t total = 0;
    for (const auto& n : caps.cuda_device_names) total += n.size() + 1;

    char* buf = static_cast<char*>(std::malloc(total));
    if (!buf) return;

    std::size_t cursor = 0;
    for (const auto& n : caps.cuda_device_names) {
        std::memcpy(buf + cursor, n.data(), n.size());
        buf[cursor + n.size()] = '\0';
        cursor += n.size() + 1;
    }

    out->cuda_device_names      = buf;
    out->cuda_device_names_size = total;
}

void COLLAPSAR_BRIDGE_CALL
collapsar_capabilities_free(collapsar_capabilities_t* caps) {
    if (!caps) return;
    if (caps->cuda_device_names) std::free(caps->cuda_device_names);
    caps->cuda_device_names      = nullptr;
    caps->cuda_device_names_size = 0;
}

// ---- Job options ------------------------------------------------------------

void COLLAPSAR_BRIDGE_CALL
collapsar_job_options_default(collapsar_job_options_t* out) {
    if (!out) return;
    out->format              = COLLAPSAR_FORMAT_ZIP;
    out->algorithm           = COLLAPSAR_ALGO_AUTO;
    out->level               = COLLAPSAR_LEVEL_BALANCED;
    out->gpu_threshold_bytes = 1ull * 1024 * 1024;
    out->gpu_batch_bytes     = 256ull * 1024 * 1024;
    out->block_bytes         = 8ull * 1024 * 1024;
    out->recurse             = 1;
    out->overwrite           = 0;
}

// ---- Job result -------------------------------------------------------------

void COLLAPSAR_BRIDGE_CALL
collapsar_job_result_free(collapsar_job_result_t* result) {
    if (!result) return;
    if (result->message) std::free(result->message);
    result->message = nullptr;
}

// ---- Engine -----------------------------------------------------------------

collapsar_engine_handle* COLLAPSAR_BRIDGE_CALL
collapsar_engine_create(int32_t cuda_device,
                        int32_t enable_gpu,
                        int32_t io_threads,
                        int32_t cpu_threads) {
    try {
        collapsar::EngineOptions opts;
        opts.cuda_device = cuda_device;
        opts.enable_gpu  = enable_gpu != 0;
        if (io_threads  > 0) opts.io_threads        = static_cast<std::size_t>(io_threads);
        if (cpu_threads > 0) opts.cpu_codec_threads = static_cast<std::size_t>(cpu_threads);

        auto* h = new collapsar_engine_handle{};
        h->engine = std::make_unique<collapsar::Engine>(opts);
        return h;
    } catch (...) {
        return nullptr;
    }
}

void COLLAPSAR_BRIDGE_CALL
collapsar_engine_destroy(collapsar_engine_handle* engine) {
    if (!engine) return;
    {
        std::scoped_lock lock{engine->job_mutex};
        if (engine->job) {
            engine->job->cancel();
            engine->job->wait();
            engine->job.reset();
        }
    }
    delete engine;
}

void COLLAPSAR_BRIDGE_CALL
collapsar_engine_pack(collapsar_engine_handle*       engine,
                      const char* const*             inputs,
                      uint64_t                       inputs_count,
                      const char*                    output,
                      const collapsar_job_options_t* options,
                      collapsar_progress_cb          on_progress,
                      void*                          user_data,
                      collapsar_job_result_t*        out_result) {
    if (!out_result) return;
    *out_result = {};

    if (!engine || !engine->engine) {
        out_result->status  = COLLAPSAR_INTERNAL;
        out_result->message = dup_cstring("engine handle is null");
        return;
    }
    if (!output) {
        out_result->status  = COLLAPSAR_INVALID_ARGUMENT;
        out_result->message = dup_cstring("output path is null");
        return;
    }

    collapsar::JobRequest req;
    req.inputs.reserve(static_cast<std::size_t>(inputs_count));
    for (uint64_t i = 0; i < inputs_count; ++i) {
        if (inputs && inputs[i]) {
            req.inputs.emplace_back(inputs[i]);
        }
    }
    req.output = output;

    if (options) {
        req.options.format               = to_native_format(options->format);
        req.options.algorithm            = to_native_algorithm(options->algorithm);
        req.options.level                = to_native_level(options->level);
        req.options.gpu_threshold_bytes  = static_cast<std::size_t>(options->gpu_threshold_bytes);
        req.options.gpu_batch_bytes      = static_cast<std::size_t>(options->gpu_batch_bytes);
        req.options.block_bytes          = static_cast<std::size_t>(options->block_bytes);
        req.options.recurse              = options->recurse  != 0;
        req.options.overwrite            = options->overwrite != 0;
    }

    if (on_progress) {
        req.on_progress = [on_progress, user_data](const collapsar::ProgressEvent& e) {
            // current_file_str outlives the callback invocation; the const
            // char* we hand out points into it.
            const std::string current_file_str = e.current_file.string();
            collapsar_progress_event_t ev{};
            ev.total_files      = e.total_files;
            ev.total_bytes      = e.total_bytes;
            ev.processed_files  = e.processed_files;
            ev.processed_bytes  = e.processed_bytes;
            ev.output_bytes     = e.output_bytes;
            ev.throughput_mibps = e.throughput_mibps;
            ev.current_file     = current_file_str.c_str();
            on_progress(&ev, user_data);
        };
    }

    auto handle_res = engine->engine->submit(std::move(req));
    if (!handle_res) {
        const auto& err = handle_res.error();
        out_result->status  = static_cast<int32_t>(err.code());
        out_result->message = dup_cstring(err.message());
        return;
    }

    auto job = std::make_shared<collapsar::JobHandle>(std::move(handle_res).value());
    {
        std::scoped_lock lock{engine->job_mutex};
        engine->job = job;
    }

    job->wait();

    auto err   = job->error();
    auto stats = job->stats();

    {
        std::scoped_lock lock{engine->job_mutex};
        engine->job.reset();
    }

    out_result->status          = static_cast<int32_t>(err.code());
    out_result->message         = dup_cstring(err.message());
    out_result->files_processed = stats.files_processed;
    out_result->input_bytes     = stats.input_bytes;
    out_result->output_bytes    = stats.output_bytes;
    out_result->cpu_blocks      = stats.cpu_blocks;
    out_result->gpu_blocks      = stats.gpu_blocks;
    out_result->elapsed_millis  = static_cast<uint64_t>(stats.elapsed.count());
}

void COLLAPSAR_BRIDGE_CALL
collapsar_engine_cancel(collapsar_engine_handle* engine) {
    if (!engine) return;
    std::shared_ptr<collapsar::JobHandle> job;
    {
        std::scoped_lock lock{engine->job_mutex};
        job = engine->job;
    }
    if (job) job->cancel();
}

}  // extern "C"
