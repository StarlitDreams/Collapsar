#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "collapsar/format.hpp"
#include "collapsar/result.hpp"

namespace collapsar {

// One compressed output produced by a codec, paired back with its source
// metadata so the assembler can match it to the right archive entry.
struct CompressedBlock {
    std::uint64_t          file_id        = 0;
    std::uint32_t          block_index    = 0;
    bool                   is_last        = false;
    std::uint64_t          logical_offset = 0;
    std::vector<std::byte> bytes;          // owned compressed payload
    std::uint32_t          crc32          = 0;  // CRC of the *uncompressed* source
    std::size_t            uncompressed   = 0;
};

// Single block — what a codec sees. The codec is allowed to keep the source
// span valid only for the duration of the call.
struct PlainBlock {
    std::uint64_t            file_id;
    std::uint32_t            block_index;
    bool                     is_last;
    std::uint64_t            logical_offset;
    std::span<const std::byte> source;
};

// Codecs are stateless after construction and may be invoked from many
// threads concurrently. The CPU codec is invoked from a thread pool; the GPU
// codec is invoked from the GPU orchestrator thread (which serialises calls
// onto CUDA streams internally).
class ICodec {
public:
    virtual ~ICodec() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual Algorithm        algorithm() const noexcept = 0;

    // Compresses one block. Implementations populate `out.bytes`,
    // `out.crc32`, and `out.uncompressed`; the caller fills in the routing
    // metadata (file_id, block_index, ...).
    [[nodiscard]] virtual Result<void> compress(const PlainBlock& in, CompressedBlock& out) = 0;
};

// Batched variant for the GPU codec. The pipeline accumulates plain blocks
// until the batch is large enough, then submits them in one nvCOMP call.
class IBatchCodec {
public:
    virtual ~IBatchCodec() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual Algorithm        algorithm() const noexcept = 0;

    [[nodiscard]] virtual Result<void> compress_batch(
        std::span<const PlainBlock> in,
        std::span<CompressedBlock>  out) = 0;
};

} // namespace collapsar
