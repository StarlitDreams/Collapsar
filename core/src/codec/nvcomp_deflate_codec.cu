#include "nvcomp_deflate_codec.hpp"

#if COLLAPSAR_HAS_CUDA

#include <algorithm>
#include <cstring>
#include <vector>

#include <cuda_runtime.h>
#include <nvcomp.h>
#include <nvcomp/deflate.h>

#include "../util/cuda_check.hpp"
#include "crc32.hpp"

namespace collapsar {

// One slot in the triple-buffered stream ring. Each slot owns the device-side
// staging memory for one in-flight batch plus a CUDA stream and an event.
struct StreamSlot {
    cudaStream_t stream     = nullptr;
    cudaEvent_t  done_event = nullptr;
    void*        d_input     = nullptr;
    void*        d_output    = nullptr;
    void*        d_temp      = nullptr;
    std::size_t  d_input_capacity  = 0;
    std::size_t  d_output_capacity = 0;
    std::size_t  d_temp_capacity   = 0;

    // Per-batch metadata mirrored device-side.
    void**         d_in_ptrs  = nullptr;
    std::size_t*   d_in_sizes = nullptr;
    void**         d_out_ptrs = nullptr;
    std::size_t*   d_out_sizes_inout = nullptr;
    nvcompStatus_t* d_statuses = nullptr;
    std::size_t    d_meta_capacity = 0;
};

struct NvcompDeflateCodec::Impl {
    int                       device   = 0;
    std::vector<StreamSlot>   slots;
    std::size_t               next_slot = 0;
    nvcompBatchedDeflateOpts_t opts = nvcompBatchedDeflateDefaultOpts;
};

namespace {

cudaError_t ensure_capacity(void*& ptr, std::size_t& cap, std::size_t needed) {
    if (cap >= needed) return cudaSuccess;
    if (ptr) cudaFree(ptr);
    ptr = nullptr;
    cap = 0;
    void* fresh = nullptr;
    auto rc = cudaMalloc(&fresh, needed);
    if (rc != cudaSuccess) return rc;
    ptr = fresh;
    cap = needed;
    return cudaSuccess;
}

template <typename T>
cudaError_t ensure_typed(T*& ptr, std::size_t& cap, std::size_t needed_count) {
    return ensure_capacity(reinterpret_cast<void*&>(ptr), cap, needed_count * sizeof(T));
}

} // namespace

NvcompDeflateCodec::NvcompDeflateCodec(std::unique_ptr<Impl> impl) noexcept
    : impl_{std::move(impl)} {}

NvcompDeflateCodec::~NvcompDeflateCodec() {
    if (!impl_) return;
    cudaSetDevice(impl_->device);
    for (auto& slot : impl_->slots) {
        if (slot.d_input)            cudaFree(slot.d_input);
        if (slot.d_output)           cudaFree(slot.d_output);
        if (slot.d_temp)             cudaFree(slot.d_temp);
        if (slot.d_in_ptrs)          cudaFree(slot.d_in_ptrs);
        if (slot.d_in_sizes)         cudaFree(slot.d_in_sizes);
        if (slot.d_out_ptrs)         cudaFree(slot.d_out_ptrs);
        if (slot.d_out_sizes_inout)  cudaFree(slot.d_out_sizes_inout);
        if (slot.d_statuses)         cudaFree(slot.d_statuses);
        if (slot.done_event)         cudaEventDestroy(slot.done_event);
        if (slot.stream)             cudaStreamDestroy(slot.stream);
    }
}

Result<std::unique_ptr<NvcompDeflateCodec>> NvcompDeflateCodec::create(
    int cuda_device, std::size_t stream_count, std::size_t per_stream_device_bytes)
{
    if (stream_count == 0) stream_count = 3;
    if (per_stream_device_bytes == 0) per_stream_device_bytes = 64ull * 1024 * 1024;

    auto impl = std::make_unique<Impl>();
    impl->device = cuda_device;
    impl->slots.resize(stream_count);

    COLLAPSAR_CUDA_TRY(cudaSetDevice(cuda_device));

    for (auto& slot : impl->slots) {
        COLLAPSAR_CUDA_TRY(cudaStreamCreateWithFlags(&slot.stream, cudaStreamNonBlocking));
        COLLAPSAR_CUDA_TRY(cudaEventCreateWithFlags(&slot.done_event, cudaEventDisableTiming));
        // Pre-allocate the staging budgets so the first batch doesn't pay
        // allocation latency. Per-batch growth still happens on demand.
        COLLAPSAR_CUDA_TRY(ensure_capacity(slot.d_input,  slot.d_input_capacity,  per_stream_device_bytes));
        COLLAPSAR_CUDA_TRY(ensure_capacity(slot.d_output, slot.d_output_capacity, per_stream_device_bytes * 2));
    }

    return std::unique_ptr<NvcompDeflateCodec>(new NvcompDeflateCodec{std::move(impl)});
}

Result<void> NvcompDeflateCodec::compress_batch(
    std::span<const PlainBlock> in,
    std::span<CompressedBlock>  out)
{
    if (in.size() != out.size()) {
        return make_error(StatusCode::InvalidArgument, "compress_batch size mismatch");
    }
    if (in.empty()) return {};

    COLLAPSAR_CUDA_TRY(cudaSetDevice(impl_->device));

    auto& slot = impl_->slots[impl_->next_slot];
    impl_->next_slot = (impl_->next_slot + 1) % impl_->slots.size();

    // Wait for the previous use of this slot to complete before reusing memory.
    COLLAPSAR_CUDA_TRY(cudaEventSynchronize(slot.done_event));

    const std::size_t batch_n = in.size();

    // Compute per-batch sizes and the maximum chunk size; nvCOMP needs the
    // upper-bound to size the output ring.
    std::vector<std::size_t> in_sizes(batch_n);
    std::vector<std::size_t> in_offsets(batch_n);
    std::size_t total_in = 0;
    std::size_t max_chunk = 0;
    for (std::size_t i = 0; i < batch_n; ++i) {
        in_sizes[i]   = in[i].source.size();
        in_offsets[i] = total_in;
        total_in     += in[i].source.size();
        max_chunk     = std::max(max_chunk, in[i].source.size());
    }

    // Ask nvCOMP for the max compressed chunk size.
    std::size_t max_out_chunk = 0;
    COLLAPSAR_NVCOMP_TRY(nvcompBatchedDeflateCompressGetMaxOutputChunkSize(
        max_chunk, impl_->opts, &max_out_chunk));

    // Ensure device-side capacities.
    COLLAPSAR_CUDA_TRY(ensure_capacity(slot.d_input,  slot.d_input_capacity,  total_in));
    COLLAPSAR_CUDA_TRY(ensure_capacity(slot.d_output, slot.d_output_capacity, max_out_chunk * batch_n));

    std::size_t temp_size = 0;
    COLLAPSAR_NVCOMP_TRY(nvcompBatchedDeflateCompressGetTempSize(
        batch_n, max_chunk, impl_->opts, &temp_size));
    COLLAPSAR_CUDA_TRY(ensure_capacity(slot.d_temp, slot.d_temp_capacity, temp_size));

    // Per-batch metadata arrays.
    COLLAPSAR_CUDA_TRY(ensure_typed(slot.d_in_ptrs,         slot.d_meta_capacity, batch_n));
    // d_meta_capacity is shared across all metadata arrays — they're equal length.
    {
        std::size_t cap = slot.d_meta_capacity;
        COLLAPSAR_CUDA_TRY(ensure_typed(slot.d_in_sizes,        cap, batch_n));
        cap = slot.d_meta_capacity;
        COLLAPSAR_CUDA_TRY(ensure_typed(slot.d_out_ptrs,        cap, batch_n));
        cap = slot.d_meta_capacity;
        COLLAPSAR_CUDA_TRY(ensure_typed(slot.d_out_sizes_inout, cap, batch_n));
        cap = slot.d_meta_capacity;
        COLLAPSAR_CUDA_TRY(ensure_typed(slot.d_statuses,        cap, batch_n));
    }

    // Build host-side pointer/size arrays into the device staging buffer.
    auto* d_in_base  = static_cast<std::byte*>(slot.d_input);
    auto* d_out_base = static_cast<std::byte*>(slot.d_output);

    std::vector<void*>          h_in_ptrs(batch_n);
    std::vector<void*>          h_out_ptrs(batch_n);
    std::vector<std::size_t>    h_out_sizes(batch_n, max_out_chunk);

    for (std::size_t i = 0; i < batch_n; ++i) {
        h_in_ptrs[i]  = d_in_base  + in_offsets[i];
        h_out_ptrs[i] = d_out_base + i * max_out_chunk;

        // H2D copy for this chunk on our stream.
        COLLAPSAR_CUDA_TRY(cudaMemcpyAsync(
            h_in_ptrs[i],
            in[i].source.data(),
            in[i].source.size(),
            cudaMemcpyHostToDevice,
            slot.stream));
    }

    // Push the metadata arrays to the device.
    COLLAPSAR_CUDA_TRY(cudaMemcpyAsync(slot.d_in_ptrs,         h_in_ptrs.data(),
                                       batch_n * sizeof(void*),       cudaMemcpyHostToDevice, slot.stream));
    COLLAPSAR_CUDA_TRY(cudaMemcpyAsync(slot.d_in_sizes,        in_sizes.data(),
                                       batch_n * sizeof(std::size_t), cudaMemcpyHostToDevice, slot.stream));
    COLLAPSAR_CUDA_TRY(cudaMemcpyAsync(slot.d_out_ptrs,        h_out_ptrs.data(),
                                       batch_n * sizeof(void*),       cudaMemcpyHostToDevice, slot.stream));
    COLLAPSAR_CUDA_TRY(cudaMemcpyAsync(slot.d_out_sizes_inout, h_out_sizes.data(),
                                       batch_n * sizeof(std::size_t), cudaMemcpyHostToDevice, slot.stream));

    // Kick off the batched compression. Output sizes will be written back into
    // d_out_sizes_inout, which we'll mirror to the host below.
    COLLAPSAR_NVCOMP_TRY(nvcompBatchedDeflateCompressAsync(
        slot.d_in_ptrs,
        slot.d_in_sizes,
        max_chunk,
        batch_n,
        slot.d_temp,
        temp_size,
        slot.d_out_ptrs,
        slot.d_out_sizes_inout,
        impl_->opts,
        slot.stream));

    // Pull the actual compressed sizes back so we know how much to D2H.
    COLLAPSAR_CUDA_TRY(cudaMemcpyAsync(h_out_sizes.data(), slot.d_out_sizes_inout,
                                       batch_n * sizeof(std::size_t), cudaMemcpyDeviceToHost, slot.stream));
    COLLAPSAR_CUDA_TRY(cudaStreamSynchronize(slot.stream));

    // Copy each compressed payload to the caller's CompressedBlock storage.
    // We do this synchronously for simplicity; with more pipelining we'd queue
    // the D2H on the stream and signal an event.
    for (std::size_t i = 0; i < batch_n; ++i) {
        out[i].bytes.resize(h_out_sizes[i]);
        COLLAPSAR_CUDA_TRY(cudaMemcpyAsync(
            out[i].bytes.data(),
            h_out_ptrs[i],
            h_out_sizes[i],
            cudaMemcpyDeviceToHost,
            slot.stream));
        out[i].uncompressed = in[i].source.size();
        // CRC32 stays on the host — nvCOMP doesn't return it, and source is
        // already in pinned memory so this is fast.
        out[i].crc32 = Crc32::compute(in[i].source);
    }

    COLLAPSAR_CUDA_TRY(cudaEventRecord(slot.done_event, slot.stream));
    COLLAPSAR_CUDA_TRY(cudaStreamSynchronize(slot.stream));
    return {};
}

} // namespace collapsar

#endif // COLLAPSAR_HAS_CUDA
