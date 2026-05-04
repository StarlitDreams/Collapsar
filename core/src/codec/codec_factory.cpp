#include "codec_factory.hpp"

#include <algorithm>

#include "collapsar/config.hpp"
#include "zlib_deflate_codec.hpp"

#if COLLAPSAR_HAS_CUDA
#  include "nvcomp_deflate_codec.hpp"
#endif

namespace collapsar {

Result<CodecBundle> make_codecs(const EngineOptions& engine, const JobOptions& job) {
    CodecBundle bundle;

    // CPU codec — always built. ZIP needs standard deflate; gzip likewise.
    // For zstd we'd plug in a zstd-based codec; not implemented yet.
    switch (job.format) {
        case Format::Zip:
        case Format::Gzip:
            bundle.cpu = std::make_unique<ZlibDeflateCodec>(job.level);
            break;
        case Format::Zstd:
            return make_error(StatusCode::Unsupported,
                              "Zstd format not yet implemented (CPU path)");
    }

    if (!engine.enable_gpu) return bundle;

#if COLLAPSAR_HAS_CUDA
    // For ZIP/Gzip + GPU we use nvCOMP's standard Deflate codec. GDeflate
    // would not produce ZIP-readable output.
    if (job.algorithm == Algorithm::GpuGDeflate && job.format != Format::Zip) {
        // GDeflate is fine for raw output but not for ZIP — leave to caller's
        // discretion. We don't currently expose a non-ZIP GDeflate path.
    }

    if (job.format == Format::Zip || job.format == Format::Gzip) {
        const std::size_t per_stream_bytes =
            static_cast<std::size_t>(engine.gpu_memory_budget_mib) * 1024 * 1024 / std::max<std::size_t>(engine.gpu_streams, 1);
        auto gpu_res = NvcompDeflateCodec::create(engine.cuda_device,
                                                   engine.gpu_streams,
                                                   per_stream_bytes);
        if (!gpu_res) return std::move(gpu_res).error();
        bundle.gpu = std::move(gpu_res).value();
    }
#else
    (void)engine;  // avoid unused-parameter warning
#endif

    return bundle;
}

} // namespace collapsar
