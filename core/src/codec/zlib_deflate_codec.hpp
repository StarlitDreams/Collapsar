#pragma once

#include "i_codec.hpp"

namespace collapsar {

// CPU Deflate codec using zlib. Each call uses a one-shot deflate so blocks
// are independent — that matches what the ZIP writer needs (one stored entry
// per block, glued by file_id at assembly time).
//
// For very small files, the level is bumped down to Fast to amortise the
// fixed setup cost; for the GPU path that's not an issue because the size
// router keeps small files on the CPU side.
class ZlibDeflateCodec final : public ICodec {
public:
    explicit ZlibDeflateCodec(Level level) noexcept;

    [[nodiscard]] std::string_view name()      const noexcept override { return "zlib-deflate"; }
    [[nodiscard]] Algorithm        algorithm() const noexcept override { return Algorithm::CpuDeflate; }

    [[nodiscard]] Result<void> compress(const PlainBlock& in, CompressedBlock& out) override;

private:
    int zlib_level_;
};

} // namespace collapsar
