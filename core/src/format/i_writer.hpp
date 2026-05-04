#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

#include "collapsar/format.hpp"
#include "collapsar/result.hpp"
#include "../codec/i_codec.hpp"
#include "../io/file_scanner.hpp"

namespace collapsar {

// Single file ready to be written, with all of its compressed blocks already
// materialised in input order. The writer is allowed to consume `blocks`.
struct PreparedFile {
    FileEntry                     entry;
    std::vector<CompressedBlock>  blocks;
    std::uint32_t                 file_crc32   = 0;
    std::uint64_t                 uncompressed = 0;
    std::uint64_t                 compressed   = 0;
};

// IWriter is invoked by the writer stage. Implementations are not thread-safe
// and are only ever touched from one thread (the assembler) within a job.
class IWriter {
public:
    virtual ~IWriter() = default;

    [[nodiscard]] virtual Result<void> begin() = 0;
    [[nodiscard]] virtual Result<void> write_file(PreparedFile&& file) = 0;
    [[nodiscard]] virtual Result<void> finish() = 0;

    [[nodiscard]] virtual std::uint64_t bytes_written() const noexcept = 0;
};

[[nodiscard]] Result<std::unique_ptr<IWriter>> make_writer(
    Format                       format,
    const std::filesystem::path& output,
    bool                          overwrite);

} // namespace collapsar
