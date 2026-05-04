#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>

#include "i_writer.hpp"

namespace collapsar {

// Single-stream gzip writer. Gzip is not an archive format — it can hold one
// file. We accept multiple inputs only when there's a single FileEntry; more
// than one returns FormatError. The stream is RFC 1952: 10-byte header, raw
// deflate payload, 8-byte trailer (CRC32 + ISIZE).
class GzipWriter final : public IWriter {
public:
    GzipWriter(std::filesystem::path output, bool overwrite);

    [[nodiscard]] Result<void> begin()                          override;
    [[nodiscard]] Result<void> write_file(PreparedFile&& file)  override;
    [[nodiscard]] Result<void> finish()                         override;

    [[nodiscard]] std::uint64_t bytes_written() const noexcept override { return bytes_written_; }

private:
    [[nodiscard]] Result<void> write_bytes(const void* data, std::size_t bytes);

    std::filesystem::path  output_;
    bool                    overwrite_;
    std::ofstream           stream_;
    std::uint64_t           bytes_written_ = 0;
    bool                    wrote_one_     = false;
};

} // namespace collapsar
