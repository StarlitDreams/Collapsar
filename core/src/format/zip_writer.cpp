#include "zip_writer.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <ctime>
#include <system_error>
#include <utility>

#include "zip_structures.hpp"

namespace collapsar {
namespace {

// Little-endian writers — the ZIP format is LE on the wire regardless of host.
void put16(std::byte* dst, std::uint16_t v) noexcept {
    dst[0] = static_cast<std::byte>(v & 0xFF);
    dst[1] = static_cast<std::byte>((v >> 8) & 0xFF);
}
void put32(std::byte* dst, std::uint32_t v) noexcept {
    dst[0] = static_cast<std::byte>(v & 0xFF);
    dst[1] = static_cast<std::byte>((v >> 8) & 0xFF);
    dst[2] = static_cast<std::byte>((v >> 16) & 0xFF);
    dst[3] = static_cast<std::byte>((v >> 24) & 0xFF);
}
void put64(std::byte* dst, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) {
        dst[i] = static_cast<std::byte>((v >> (i * 8)) & 0xFF);
    }
}

// Convert a filesystem time into the legacy DOS date/time pair used by ZIP.
std::pair<std::uint16_t, std::uint16_t> dos_time(std::filesystem::file_time_type ftime) {
    using namespace std::chrono;
    const auto sys = time_point_cast<system_clock::duration>(
        ftime - decltype(ftime)::clock::now() + system_clock::now());
    const std::time_t t = system_clock::to_time_t(sys);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    if (tm.tm_year + 1900 < 1980) {
        // ZIP epoch is 1980-01-01; clamp.
        tm = std::tm{};
        tm.tm_year = 80;  // 1980
        tm.tm_mon  = 0;
        tm.tm_mday = 1;
    }
    const std::uint16_t dos_t = static_cast<std::uint16_t>(
        ((tm.tm_hour & 0x1F) << 11) |
        ((tm.tm_min  & 0x3F) << 5)  |
        ((tm.tm_sec / 2) & 0x1F));
    const std::uint16_t dos_d = static_cast<std::uint16_t>(
        (((tm.tm_year + 1900 - 1980) & 0x7F) << 9) |
        (((tm.tm_mon + 1) & 0x0F) << 5) |
        (tm.tm_mday & 0x1F));
    return {dos_t, dos_d};
}

std::string sanitise_archive_name(const std::filesystem::path& p) {
    // ZIP entries use forward slashes regardless of host OS (APPNOTE 4.4.17.1).
    auto generic = p.generic_string();
    while (!generic.empty() && (generic.front() == '/' || generic.front() == '\\')) {
        generic.erase(generic.begin());
    }
    return generic;
}

} // namespace

bool ZipWriter::CentralEntry::needs_zip64() const noexcept {
    return compressed   >= zip::U32_MAX
        || uncompressed >= zip::U32_MAX
        || lfh_offset   >= zip::U32_MAX;
}

ZipWriter::ZipWriter(std::filesystem::path output, bool overwrite)
    : output_{std::move(output)}, overwrite_{overwrite} {}

Result<void> ZipWriter::begin() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (fs::exists(output_, ec) && !overwrite_) {
        return make_error(StatusCode::InvalidArgument,
                          "Output already exists (pass overwrite=true to replace): " + output_.string());
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

Result<void> ZipWriter::write_bytes(const void* data, std::size_t bytes) {
    stream_.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
    if (!stream_) {
        return make_error(StatusCode::Io, "Short write to " + output_.string());
    }
    bytes_written_ += bytes;
    return {};
}

Result<void> ZipWriter::write_local_header(const CentralEntry& entry) {
    const bool        zip64    = entry.needs_zip64();
    const std::uint16_t version = zip64 ? zip::VERSION_ZIP64 : zip::VERSION_DEFLATE;

    // Static portion of the LFH is 30 bytes.
    std::array<std::byte, 30> hdr{};
    put32(&hdr[0],  zip::LFH_SIGNATURE);
    put16(&hdr[4],  version);
    put16(&hdr[6],  0);                   // general purpose bit flag
    put16(&hdr[8],  entry.method);
    put16(&hdr[10], entry.mod_time);
    put16(&hdr[12], entry.mod_date);
    put32(&hdr[14], entry.crc32);
    put32(&hdr[18], zip64 ? zip::U32_MAX : static_cast<std::uint32_t>(entry.compressed));
    put32(&hdr[22], zip64 ? zip::U32_MAX : static_cast<std::uint32_t>(entry.uncompressed));
    put16(&hdr[26], static_cast<std::uint16_t>(entry.name.size()));
    put16(&hdr[28], static_cast<std::uint16_t>(zip64 ? 20 : 0));  // extra field len

    if (auto r = write_bytes(hdr.data(), hdr.size()); !r) return r;
    if (auto r = write_bytes(entry.name.data(), entry.name.size()); !r) return r;

    if (zip64) {
        // Zip64 extra field: header ID (2) + size (2) + uncompressed (8) + compressed (8).
        std::array<std::byte, 20> extra{};
        put16(&extra[0],  zip::EXTRA_ID_ZIP64);
        put16(&extra[2],  16);
        put64(&extra[4],  entry.uncompressed);
        put64(&extra[12], entry.compressed);
        if (auto r = write_bytes(extra.data(), extra.size()); !r) return r;
    }
    return {};
}

Result<void> ZipWriter::write_file(PreparedFile&& file) {
    CentralEntry entry{
        .name         = sanitise_archive_name(file.entry.archive_path),
        .method       = zip::METHOD_DEFLATE,
        .crc32        = file.file_crc32,
        .compressed   = file.compressed,
        .uncompressed = file.uncompressed,
        .lfh_offset   = bytes_written_,
    };
    auto [t, d] = dos_time(file.entry.mtime);
    entry.mod_time = t;
    entry.mod_date = d;

    if (auto r = write_local_header(entry); !r) return r;

    // Stream each compressed block straight to disk.
    for (const auto& block : file.blocks) {
        if (auto r = write_bytes(block.bytes.data(), block.bytes.size()); !r) return r;
    }

    if (entry.needs_zip64()) needs_zip64_ = true;
    central_.push_back(std::move(entry));
    return {};
}

Result<void> ZipWriter::write_central_entry(const CentralEntry& entry) {
    const bool        zip64    = entry.needs_zip64();
    const std::uint16_t version = zip64 ? zip::VERSION_ZIP64 : zip::VERSION_DEFLATE;

    std::array<std::byte, 46> hdr{};
    put32(&hdr[0],  zip::CDH_SIGNATURE);
    put16(&hdr[4],  version);             // version made by
    put16(&hdr[6],  version);             // version needed
    put16(&hdr[8],  0);                   // general purpose bit flag
    put16(&hdr[10], entry.method);
    put16(&hdr[12], entry.mod_time);
    put16(&hdr[14], entry.mod_date);
    put32(&hdr[16], entry.crc32);
    put32(&hdr[20], zip64 ? zip::U32_MAX : static_cast<std::uint32_t>(entry.compressed));
    put32(&hdr[24], zip64 ? zip::U32_MAX : static_cast<std::uint32_t>(entry.uncompressed));
    put16(&hdr[28], static_cast<std::uint16_t>(entry.name.size()));

    // Build the Zip64 extra field if needed (variable size: only present
    // overflow fields are emitted).
    std::array<std::byte, 28> extra{};
    std::size_t                extra_len = 0;
    if (zip64) {
        std::size_t cursor = 4;  // skip header for now
        if (entry.uncompressed >= zip::U32_MAX) { put64(&extra[cursor], entry.uncompressed); cursor += 8; }
        if (entry.compressed   >= zip::U32_MAX) { put64(&extra[cursor], entry.compressed);   cursor += 8; }
        if (entry.lfh_offset   >= zip::U32_MAX) { put64(&extra[cursor], entry.lfh_offset);   cursor += 8; }
        put16(&extra[0], zip::EXTRA_ID_ZIP64);
        put16(&extra[2], static_cast<std::uint16_t>(cursor - 4));
        extra_len = cursor;
    }
    put16(&hdr[30], static_cast<std::uint16_t>(extra_len));
    put16(&hdr[32], 0);                   // file comment length
    put16(&hdr[34], 0);                   // disk number start
    put16(&hdr[36], 0);                   // internal attrs
    put32(&hdr[38], 0);                   // external attrs
    put32(&hdr[42], zip64 ? zip::U32_MAX : static_cast<std::uint32_t>(entry.lfh_offset));

    if (auto r = write_bytes(hdr.data(), hdr.size()); !r) return r;
    if (auto r = write_bytes(entry.name.data(), entry.name.size()); !r) return r;
    if (extra_len) {
        if (auto r = write_bytes(extra.data(), extra_len); !r) return r;
    }
    return {};
}

Result<void> ZipWriter::write_eocd() {
    const std::uint64_t cd_offset = bytes_written_;
    for (const auto& entry : central_) {
        if (auto r = write_central_entry(entry); !r) return r;
    }
    const std::uint64_t cd_size = bytes_written_ - cd_offset;

    const bool zip64 = needs_zip64_
        || central_.size() >= zip::U16_MAX
        || cd_size  >= zip::U32_MAX
        || cd_offset >= zip::U32_MAX;

    if (zip64) {
        // Zip64 EOCD record.
        std::array<std::byte, 56> z64{};
        put32(&z64[0],  zip::ZIP64_EOCD_SIGNATURE);
        put64(&z64[4],  44);                                   // size of remainder
        put16(&z64[12], zip::VERSION_ZIP64);                   // made by
        put16(&z64[14], zip::VERSION_ZIP64);                   // needed
        put32(&z64[16], 0);                                    // disk number
        put32(&z64[20], 0);                                    // disk with CD
        put64(&z64[24], central_.size());
        put64(&z64[32], central_.size());
        put64(&z64[40], cd_size);
        put64(&z64[48], cd_offset);
        if (auto r = write_bytes(z64.data(), z64.size()); !r) return r;

        // Zip64 EOCD locator.
        const std::uint64_t z64_eocd_offset = cd_offset + cd_size;
        std::array<std::byte, 20> loc{};
        put32(&loc[0],  zip::ZIP64_EOCD_LOCATOR_SIG);
        put32(&loc[4],  0);
        put64(&loc[8],  z64_eocd_offset);
        put32(&loc[16], 1);
        if (auto r = write_bytes(loc.data(), loc.size()); !r) return r;
    }

    // Classic EOCD — always written, even when Zip64 is in use, so simple
    // tools can still locate the archive.
    std::array<std::byte, 22> eocd{};
    put32(&eocd[0],  zip::EOCD_SIGNATURE);
    put16(&eocd[4],  0);
    put16(&eocd[6],  0);
    put16(&eocd[8],  zip64 ? zip::U16_MAX : static_cast<std::uint16_t>(central_.size()));
    put16(&eocd[10], zip64 ? zip::U16_MAX : static_cast<std::uint16_t>(central_.size()));
    put32(&eocd[12], zip64 ? zip::U32_MAX : static_cast<std::uint32_t>(cd_size));
    put32(&eocd[16], zip64 ? zip::U32_MAX : static_cast<std::uint32_t>(cd_offset));
    put16(&eocd[20], 0);
    return write_bytes(eocd.data(), eocd.size());
}

Result<void> ZipWriter::finish() {
    if (auto r = write_eocd(); !r) return r;
    stream_.flush();
    if (!stream_) {
        return make_error(StatusCode::Io, "Failed to flush " + output_.string());
    }
    stream_.close();
    return {};
}

} // namespace collapsar
