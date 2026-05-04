#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace collapsar {

// Streaming CRC32 (poly 0xEDB88320, init 0, xorout 0xFFFFFFFF) — the variant
// used by ZIP and gzip. Delegates to zlib's crc32_z which uses slicing-by-8 on
// most builds, so this is comfortably I/O bound.
//
// Block-level CRCs are combined with crc32_combine so we can checksum each
// block independently and stitch them back together for the file CRC.
class Crc32 {
public:
    Crc32() noexcept = default;

    void           update(std::span<const std::byte> data) noexcept;
    [[nodiscard]] std::uint32_t value() const noexcept { return value_; }

    // Compute CRC of one isolated buffer.
    [[nodiscard]] static std::uint32_t compute(std::span<const std::byte> data) noexcept;

    // Combine two block CRCs into one (CRC of concatenated data). `len2` is
    // the length, in bytes, of the second region.
    [[nodiscard]] static std::uint32_t combine(std::uint32_t crc1,
                                                std::uint32_t crc2,
                                                std::uint64_t len2) noexcept;

private:
    std::uint32_t value_ = 0;
};

} // namespace collapsar
