#pragma once

#include "collapsar/config.hpp"
#include "i_codec.hpp"

#if COLLAPSAR_HAS_CUDA

#include <cstddef>
#include <cstdint>
#include <memory>

namespace collapsar {

// GPU batched Deflate codec backed by nvCOMP.
//
// One instance owns a small ring of triple-buffered CUDA streams plus pinned
// staging regions. The pipeline calls compress_batch() with a batch of plain
// blocks (already in pinned host memory) and the codec:
//   1. asynchronously copies the inputs H2D on stream s0
//   2. launches nvcompBatchedDeflateCompressAsync   on stream s1
//   3. asynchronously copies outputs D2H            on stream s2
// using cudaEvents to chain the three steps. While one batch is in step 3 the
// next batch is already in step 1, hiding PCIe latency behind compute.
class NvcompDeflateCodec final : public IBatchCodec {
public:
    static Result<std::unique_ptr<NvcompDeflateCodec>> create(
        int        cuda_device,
        std::size_t streams,
        std::size_t per_stream_device_bytes);

    ~NvcompDeflateCodec();

    [[nodiscard]] std::string_view name()      const noexcept override { return "nvcomp-deflate"; }
    [[nodiscard]] Algorithm        algorithm() const noexcept override { return Algorithm::GpuDeflate; }

    [[nodiscard]] Result<void> compress_batch(
        std::span<const PlainBlock> in,
        std::span<CompressedBlock>  out) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    explicit NvcompDeflateCodec(std::unique_ptr<Impl> impl) noexcept;
};

} // namespace collapsar

#endif // COLLAPSAR_HAS_CUDA
