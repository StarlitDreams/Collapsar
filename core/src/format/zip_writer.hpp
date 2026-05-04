#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "i_writer.hpp"

namespace collapsar {

// ZIP archive writer. Streams local file headers + compressed payloads as
// each PreparedFile arrives, and emits the central directory + EOCD on
// finish(). Promotes to Zip64 transparently when needed (any single file
// over 4 GiB, more than 65k entries, or total archive size over 4 GiB).
class ZipWriter final : public IWriter {
public:
    ZipWriter(std::filesystem::path output, bool overwrite);

    [[nodiscard]] Result<void> begin()                              override;
    [[nodiscard]] Result<void> write_file(PreparedFile&& file)      override;
    [[nodiscard]] Result<void> finish()                             override;

    [[nodiscard]] std::uint64_t bytes_written() const noexcept override { return bytes_written_; }

private:
    struct CentralEntry {
        std::string  name;
        std::uint16_t method        = 0;
        std::uint32_t crc32         = 0;
        std::uint64_t compressed    = 0;
        std::uint64_t uncompressed  = 0;
        std::uint64_t lfh_offset    = 0;
        std::uint16_t mod_time      = 0;
        std::uint16_t mod_date      = 0;
        bool          needs_zip64() const noexcept;
    };

    [[nodiscard]] Result<void> write_local_header(const CentralEntry& entry);
    [[nodiscard]] Result<void> write_central_entry(const CentralEntry& entry);
    [[nodiscard]] Result<void> write_eocd();

    [[nodiscard]] Result<void> write_bytes(const void* data, std::size_t bytes);

    std::filesystem::path     output_;
    bool                       overwrite_;
    std::ofstream              stream_;
    std::vector<CentralEntry>  central_;
    std::uint64_t              bytes_written_ = 0;
    bool                       needs_zip64_   = false;
};

} // namespace collapsar
