#include "pinned_buffer.hpp"

#include <cstdlib>
#include <new>
#include <utility>

#if COLLAPSAR_HAS_CUDA
#  include <cuda_runtime.h>
#  include "../util/cuda_check.hpp"
#endif

namespace collapsar {

PinnedBuffer::PinnedBuffer(PinnedBuffer&& other) noexcept
    : data_{std::exchange(other.data_, nullptr)},
      size_{std::exchange(other.size_, 0)} {}

PinnedBuffer& PinnedBuffer::operator=(PinnedBuffer&& other) noexcept {
    if (this != &other) {
        release();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

PinnedBuffer::~PinnedBuffer() { release(); }

void PinnedBuffer::release() noexcept {
    if (!data_) return;
#if COLLAPSAR_HAS_CUDA
    cudaFreeHost(data_);
#else
    std::free(data_);
#endif
    data_ = nullptr;
    size_ = 0;
}

Result<PinnedBuffer> PinnedBuffer::allocate(std::size_t bytes) {
    if (bytes == 0) return PinnedBuffer{nullptr, 0};

    void* ptr = nullptr;
#if COLLAPSAR_HAS_CUDA
    const cudaError_t status = cudaHostAlloc(&ptr, bytes, cudaHostAllocDefault);
    if (status != cudaSuccess) {
        return cuda::from_cuda(status, "cudaHostAlloc", __FILE__, __LINE__);
    }
#else
    // 64-byte alignment matches typical cache lines and SIMD widths.
    constexpr std::size_t alignment = 64;
    const std::size_t padded = (bytes + alignment - 1) & ~(alignment - 1);
#  if defined(_WIN32)
    ptr = _aligned_malloc(padded, alignment);
#  else
    if (posix_memalign(&ptr, alignment, padded) != 0) ptr = nullptr;
#  endif
    if (!ptr) {
        return make_error(StatusCode::OutOfMemory,
                          "Failed to allocate " + std::to_string(bytes) + " bytes for pinned buffer");
    }
#endif
    return PinnedBuffer{static_cast<std::byte*>(ptr), bytes};
}

} // namespace collapsar
