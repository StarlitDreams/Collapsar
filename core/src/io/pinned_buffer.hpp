#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "collapsar/config.hpp"
#include "collapsar/result.hpp"

namespace collapsar {

// RAII wrapper over a page-locked host buffer. When CUDA is available the
// allocation comes from cudaHostAlloc so it can be DMA'd to device memory
// without an intermediate copy. Without CUDA we fall back to a plain aligned
// allocation so the rest of the pipeline doesn't need to care.
class PinnedBuffer {
public:
    PinnedBuffer() noexcept = default;
    ~PinnedBuffer();

    PinnedBuffer(const PinnedBuffer&) = delete;
    PinnedBuffer& operator=(const PinnedBuffer&) = delete;

    PinnedBuffer(PinnedBuffer&& other) noexcept;
    PinnedBuffer& operator=(PinnedBuffer&& other) noexcept;

    // Allocate `bytes` of pinned memory. Returns an error if allocation fails.
    [[nodiscard]] static Result<PinnedBuffer> allocate(std::size_t bytes);

    [[nodiscard]] std::byte*       data()       noexcept { return data_; }
    [[nodiscard]] const std::byte* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t      size() const noexcept { return size_; }

    [[nodiscard]] std::span<std::byte>       span()       noexcept { return {data_, size_}; }
    [[nodiscard]] std::span<const std::byte> span() const noexcept { return {data_, size_}; }

private:
    PinnedBuffer(std::byte* data, std::size_t bytes) noexcept
        : data_{data}, size_{bytes} {}

    void release() noexcept;

    std::byte*  data_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace collapsar
