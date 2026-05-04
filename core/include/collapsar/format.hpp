#pragma once

#include <string_view>

namespace collapsar {

// Output container format. Each maps onto a writer in core/src/format.
enum class Format {
    Zip,
    Gzip,
    Zstd,
};

// Codec algorithm hint. Auto lets the size router pick CPU vs GPU.
enum class Algorithm {
    Auto,
    CpuDeflate,   // zlib / zlib-ng on the host
    GpuDeflate,   // nvCOMP batched standard Deflate (RFC 1951) — ZIP-compatible
    GpuGDeflate,  // nvCOMP batched GDeflate — proprietary, raw output only
    CpuZstd,
    GpuZstd,
};

// Compression effort. CPU codecs map this onto numeric levels; GPU codecs
// expose only what nvCOMP supports.
enum class Level {
    Fast,
    Balanced,
    Best,
};

[[nodiscard]] constexpr std::string_view extension_for(Format f) noexcept {
    switch (f) {
        case Format::Zip:  return ".zip";
        case Format::Gzip: return ".gz";
        case Format::Zstd: return ".zst";
    }
    return "";
}

[[nodiscard]] constexpr bool is_archive(Format f) noexcept {
    return f == Format::Zip;  // gzip / zstd are single-stream containers.
}

} // namespace collapsar
