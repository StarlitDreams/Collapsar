#include "Bridge.h"

#include <memory>
#include <string>
#include <vector>

#include "collapsar/engine.hpp"

#include <msclr/marshal_cppstd.h>
#include <vcclr.h>

using namespace System;
using namespace System::Runtime::InteropServices;

namespace Collapsar {
namespace Bridge {
namespace {

// Convert managed string to std::string using marshal_context — handles UTF-8
// conversion correctly for paths with non-ASCII characters.
std::string to_std(System::String^ s) {
    if (s == nullptr) return {};
    msclr::interop::marshal_context ctx;
    return ctx.marshal_as<std::string>(s);
}

System::String^ to_managed(const std::string& s) {
    return gcnew System::String(s.c_str(), 0, static_cast<int>(s.size()), System::Text::Encoding::UTF8);
}

collapsar::Format        to_native(Bridge::Format    f) { return static_cast<collapsar::Format>(static_cast<int>(f)); }
collapsar::Algorithm     to_native(Bridge::Algorithm a) { return static_cast<collapsar::Algorithm>(static_cast<int>(a)); }
collapsar::Level         to_native(Bridge::Level     l) { return static_cast<collapsar::Level>(static_cast<int>(l)); }
Bridge::StatusCode       to_managed(collapsar::StatusCode c) { return static_cast<Bridge::StatusCode>(static_cast<int>(c)); }

ProgressSnapshot^ to_managed(const collapsar::ProgressEvent& e) {
    auto snap = gcnew ProgressSnapshot();
    snap->TotalFiles      = e.total_files;
    snap->TotalBytes      = e.total_bytes;
    snap->ProcessedFiles  = e.processed_files;
    snap->ProcessedBytes  = e.processed_bytes;
    snap->OutputBytes     = e.output_bytes;
    snap->CurrentFile     = to_managed(e.current_file.string());
    snap->ThroughputMiBps = e.throughput_mibps;
    return snap;
}

// Holds the native engine on the unmanaged heap. Bridged through IntPtr so
// the C++/CLI ref class doesn't need to embed the native type directly.
struct NativeEngineHandle {
    std::unique_ptr<collapsar::Engine> engine;
};

struct NativeJobHandle {
    std::unique_ptr<collapsar::JobHandle> job;
};

NativeEngineHandle* unwrap_engine(System::IntPtr ptr) {
    return reinterpret_cast<NativeEngineHandle*>(ptr.ToPointer());
}

NativeJobHandle* unwrap_job(System::IntPtr ptr) {
    return reinterpret_cast<NativeJobHandle*>(ptr.ToPointer());
}

} // namespace

// ---------------- JobOptions --------------------------------------------------

JobOptions::JobOptions() {
    format_            = Bridge::Format::Zip;
    algorithm_         = Bridge::Algorithm::Auto;
    level_             = Bridge::Level::Balanced;
    GpuThresholdBytes  = 1ull * 1024 * 1024;
    GpuBatchBytes      = 256ull * 1024 * 1024;
    BlockBytes         = 8ull * 1024 * 1024;
    Recurse            = true;
    Overwrite          = false;
}

Bridge::Format    JobOptions::Format::get()                              { return format_;    }
void              JobOptions::Format::set(Bridge::Format v)              { format_ = v;       }
Bridge::Algorithm JobOptions::Algorithm::get()                           { return algorithm_; }
void              JobOptions::Algorithm::set(Bridge::Algorithm v)        { algorithm_ = v;    }
Bridge::Level     JobOptions::Level::get()                               { return level_;     }
void              JobOptions::Level::set(Bridge::Level v)                { level_ = v;        }

// ---------------- CompressionEngine -------------------------------------------

CompressionEngine::CompressionEngine() : CompressionEngine(0, true, 4, 0) {}

CompressionEngine::CompressionEngine(int cudaDevice, bool enableGpu, int ioThreads, int cpuThreads) {
    collapsar::EngineOptions opts;
    opts.cuda_device       = cudaDevice;
    opts.enable_gpu        = enableGpu;
    opts.io_threads        = ioThreads > 0 ? static_cast<std::size_t>(ioThreads) : 4u;
    if (cpuThreads > 0) opts.cpu_codec_threads = static_cast<std::size_t>(cpuThreads);

    auto* h = new NativeEngineHandle{std::make_unique<collapsar::Engine>(opts)};
    enginePtr_     = System::IntPtr(h);
    currentJobPtr_ = System::IntPtr::Zero;
}

CompressionEngine::~CompressionEngine() {
    this->!CompressionEngine();
}

CompressionEngine::!CompressionEngine() {
    if (currentJobPtr_ != System::IntPtr::Zero) {
        delete unwrap_job(currentJobPtr_);
        currentJobPtr_ = System::IntPtr::Zero;
    }
    if (enginePtr_ != System::IntPtr::Zero) {
        delete unwrap_engine(enginePtr_);
        enginePtr_ = System::IntPtr::Zero;
    }
}

CapabilitiesInfo^ CompressionEngine::ProbeCapabilities() {
    auto caps = collapsar::probe_capabilities();
    auto info = gcnew CapabilitiesInfo();
    info->HasCuda         = caps.has_cuda;
    info->CudaDeviceCount = caps.cuda_device_count;
    info->CudaDeviceNames = gcnew array<System::String^>(static_cast<int>(caps.cuda_device_names.size()));
    for (int i = 0; i < info->CudaDeviceNames->Length; ++i) {
        info->CudaDeviceNames[i] = to_managed(caps.cuda_device_names[static_cast<std::size_t>(i)]);
    }
    return info;
}

JobResult^ CompressionEngine::Pack(array<System::String^>^ inputs,
                                    System::String^         output,
                                    JobOptions^             options,
                                    ProgressDelegate^       onProgress)
{
    if (enginePtr_ == System::IntPtr::Zero) {
        auto r = gcnew JobResult();
        r->Status  = StatusCode::Internal;
        r->Message = "Engine has been disposed";
        return r;
    }

    collapsar::JobRequest req;
    req.inputs.reserve(static_cast<std::size_t>(inputs->Length));
    for (int i = 0; i < inputs->Length; ++i) {
        req.inputs.emplace_back(to_std(inputs[i]));
    }
    req.output = to_std(output);

    if (options != nullptr) {
        req.options.format               = to_native(options->Format);
        req.options.algorithm            = to_native(options->Algorithm);
        req.options.level                = to_native(options->Level);
        req.options.gpu_threshold_bytes  = static_cast<std::size_t>(options->GpuThresholdBytes);
        req.options.gpu_batch_bytes      = static_cast<std::size_t>(options->GpuBatchBytes);
        req.options.block_bytes          = static_cast<std::size_t>(options->BlockBytes);
        req.options.recurse              = options->Recurse;
        req.options.overwrite            = options->Overwrite;
    }

    // The progress callback is invoked from the native pipeline thread. We
    // marshal each event through a GCHandle-rooted delegate so the GC knows
    // the delegate is reachable.
    if (onProgress != nullptr) {
        auto handle = GCHandle::Alloc(onProgress);
        IntPtr gch_ptr = GCHandle::ToIntPtr(handle);
        // Release the handle when the job finishes — done via a shared_ptr
        // cleanup below.
        struct CleanupGuard {
            IntPtr ptr;
            ~CleanupGuard() {
                if (ptr != IntPtr::Zero) {
                    GCHandle::FromIntPtr(ptr).Free();
                }
            }
        };

        const std::shared_ptr<CleanupGuard> guard{new CleanupGuard{gch_ptr}};
        req.on_progress = [gch_ptr, guard](const collapsar::ProgressEvent& e) {
            auto delegate = static_cast<ProgressDelegate^>(GCHandle::FromIntPtr(gch_ptr).Target);
            if (delegate != nullptr) delegate(to_managed(e));
        };
    }

    auto* engine = unwrap_engine(enginePtr_)->engine.get();

    // Cancel any prior in-flight job before starting a new one.
    if (currentJobPtr_ != System::IntPtr::Zero) {
        delete unwrap_job(currentJobPtr_);
        currentJobPtr_ = System::IntPtr::Zero;
    }

    auto handle_res = engine->submit(std::move(req));
    if (!handle_res) {
        auto r = gcnew JobResult();
        r->Status  = to_managed(handle_res.error().code());
        r->Message = to_managed(handle_res.error().message());
        return r;
    }

    auto* nh = new NativeJobHandle{std::make_unique<collapsar::JobHandle>(std::move(handle_res).value())};
    currentJobPtr_ = System::IntPtr(nh);

    nh->job->wait();
    auto err   = nh->job->error();
    auto stats = nh->job->stats();

    delete nh;
    currentJobPtr_ = System::IntPtr::Zero;

    auto r = gcnew JobResult();
    r->Status         = to_managed(err.code());
    r->Message        = to_managed(err.message());
    r->FilesProcessed = stats.files_processed;
    r->InputBytes     = stats.input_bytes;
    r->OutputBytes    = stats.output_bytes;
    r->CpuBlocks      = stats.cpu_blocks;
    r->GpuBlocks      = stats.gpu_blocks;
    r->ElapsedMillis  = static_cast<System::UInt64>(stats.elapsed.count());
    return r;
}

void CompressionEngine::Cancel() {
    if (currentJobPtr_ != System::IntPtr::Zero) {
        unwrap_job(currentJobPtr_)->job->cancel();
    }
}

} // namespace Bridge
} // namespace Collapsar
