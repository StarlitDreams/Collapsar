#include "zlib_deflate_codec.hpp"

#include <zlib.h>

#include "crc32.hpp"

namespace collapsar {
namespace {

int level_to_zlib(Level level) noexcept {
    switch (level) {
        case Level::Fast:     return Z_BEST_SPEED;        // 1
        case Level::Balanced: return Z_DEFAULT_COMPRESSION; // -1 → 6
        case Level::Best:     return Z_BEST_COMPRESSION;  // 9
    }
    return Z_DEFAULT_COMPRESSION;
}

} // namespace

ZlibDeflateCodec::ZlibDeflateCodec(Level level) noexcept
    : zlib_level_{level_to_zlib(level)} {}

Result<void> ZlibDeflateCodec::compress(const PlainBlock& in, CompressedBlock& out) {
    z_stream stream{};
    // -MAX_WBITS produces a raw deflate stream (no zlib header, no gzip
    // wrapper) — exactly what the ZIP local file header expects to follow.
    if (deflateInit2(&stream, zlib_level_, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        return make_error(StatusCode::CodecError, "deflateInit2 failed");
    }

    const auto upper = deflateBound(&stream, static_cast<uLong>(in.source.size()));
    out.bytes.resize(upper);

    stream.next_in   = const_cast<Bytef*>(reinterpret_cast<const Bytef*>(in.source.data()));
    stream.avail_in  = static_cast<uInt>(in.source.size());
    stream.next_out  = reinterpret_cast<Bytef*>(out.bytes.data());
    stream.avail_out = static_cast<uInt>(out.bytes.size());

    const int rc = deflate(&stream, Z_FINISH);
    if (rc != Z_STREAM_END) {
        deflateEnd(&stream);
        return make_error(StatusCode::CodecError,
                          "deflate did not finish (rc=" + std::to_string(rc) + ")");
    }
    out.bytes.resize(stream.total_out);
    deflateEnd(&stream);

    out.uncompressed = in.source.size();
    out.crc32        = Crc32::compute(in.source);
    return {};
}

} // namespace collapsar
