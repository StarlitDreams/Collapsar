#include "gzip_writer.hpp"

#include <array>
#include <chrono>
#include <ctime>
#include <system_error>
#include <utility>

namespace collapsar {
namespace {

void put32_le(std::byte* dst, std::uint32_t v) noexcept {
    dst[0] = static_cast<std::byte>(v & 0xFF);
    dst[1] = static_cast<std::byte>((v >> 8) & 0xFF);
    dst[2] = static_cast<std::byte>((v >> 16) & 0xFF);
    dst[3] = static_cast<std::byte>((v >> 24) & 0xFF);
}

std::uint32_t mtime_seconds(std::filesystem::file_time_type ftime) {
    using namespace std::chrono;
    const auto sys = time_point_cast<system_clock::duration>(
        ftime - decltype(ftime)::clock::now() + system_clock::now());
    const auto t = system_clock::to_time_t(sys);
    return static_cast<std::uint32_t>(t < 0 ? 0 : t);
}

} // namespace

GzipWriter::GzipWriter(std::filesystem::path output, bool overwrite)
    : output_{std::move(output)}, overwrite_{overwrite} {}

Result<void> GzipWriter::begin() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(output_, ec) && !overwrite_) {
        return make_error(StatusCode::InvalidArgument,
                          "Output already exists: " + output_.string());
    }
    if (auto parent = output_.parent_path(); !parent.empty()) {
        fs::create_directories(parent, ec);
    }
    stream_.open(output_, std::ios::binary | std::ios::trunc);
    if (!stream_) {
        return make_error(StatusCode::Io, "Failed to open output: " + output_.string());
    }
    return {};
}

Result<void> GzipWriter::write_bytes(const void* data, std::size_t bytes) {
    stream_.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    if (!stream_) {
        return make_error(StatusCode::Io, "Short write");
    }
    bytes_written_ += bytes;
    return {};
}

Result<void> GzipWriter::write_file(PreparedFile&& file) {
    if (wrote_one_) {
        return make_error(StatusCode::FormatError,
                          "Gzip is a single-stream format; combine inputs into a tar first");
    }
    wrote_one_ = true;

    // RFC 1952 fixed 10-byte header.
    std::array<std::byte, 10> header{
        std::byte{0x1f}, std::byte{0x8b},   // magic
        std::byte{0x08},                     // method = deflate
        std::byte{0x00},                     // flags  = none (no filename, etc.)
        std::byte{0}, std::byte{0}, std::byte{0}, std::byte{0},  // mtime
        std::byte{0x00},                     // extra flags
        std::byte{0xff},                     // OS = unknown
    };
    put32_le(&header[4], mtime_seconds(file.entry.mtime));
    if (auto r = write_bytes(header.data(), header.size()); !r) return r;

    for (const auto& block : file.blocks) {
        if (auto r = write_bytes(block.bytes.data(), block.bytes.size()); !r) return r;
    }

    std::array<std::byte, 8> trailer{};
    put32_le(&trailer[0], file.file_crc32);
    put32_le(&trailer[4], static_cast<std::uint32_t>(file.uncompressed));  // ISIZE mod 2^32
    return write_bytes(trailer.data(), trailer.size());
}

Result<void> GzipWriter::finish() {
    stream_.flush();
    if (!stream_) {
        return make_error(StatusCode::Io, "Failed to flush " + output_.string());
    }
    stream_.close();
    return {};
}

} // namespace collapsar
