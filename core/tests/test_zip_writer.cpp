#include "test_harness.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#include "codec/i_codec.hpp"
#include "format/i_writer.hpp"
#include "format/zip_structures.hpp"
#include "io/file_scanner.hpp"

using namespace collapsar;
using collapsar::test::run;
using collapsar::test::Case;

namespace {

namespace fs = std::filesystem;

fs::path temp_dir() {
    auto base = fs::temp_directory_path() / "collapsar-tests";
    fs::create_directories(base);
    return base;
}

std::vector<std::byte> read_file(const fs::path& p) {
    std::ifstream is(p, std::ios::binary);
    std::vector<std::byte> out;
    is.seekg(0, std::ios::end);
    out.resize(static_cast<std::size_t>(is.tellg()));
    is.seekg(0);
    is.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return out;
}

std::uint16_t read16_le(const std::byte* p) {
    return static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[0]))
         | static_cast<std::uint16_t>(static_cast<std::uint8_t>(p[1]) << 8);
}
std::uint32_t read32_le(const std::byte* p) {
    return static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[0]))
         | static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[1]) << 8)
         | static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[2]) << 16)
         | static_cast<std::uint32_t>(static_cast<std::uint8_t>(p[3]) << 24);
}

void writes_a_recognisable_header_and_eocd() {
    auto dir = temp_dir();
    auto out = dir / "minimal.zip";
    std::error_code ec;
    fs::remove(out, ec);

    auto writer_res = make_writer(Format::Zip, out, /*overwrite=*/true);
    COLLAPSAR_REQUIRE(static_cast<bool>(writer_res));
    auto writer = std::move(writer_res).value();

    COLLAPSAR_REQUIRE(static_cast<bool>(writer->begin()));

    // Build one PreparedFile with a stored (0-byte) payload to exercise the
    // header/CD/EOCD machinery without dragging the codec in.
    PreparedFile pf{};
    pf.entry.id           = 1;
    pf.entry.archive_path = "hello.bin";
    pf.entry.size_bytes   = 0;
    pf.file_crc32         = 0;
    pf.uncompressed       = 0;
    pf.compressed         = 0;
    // No blocks — empty file.
    COLLAPSAR_REQUIRE(static_cast<bool>(writer->write_file(std::move(pf))));
    COLLAPSAR_REQUIRE(static_cast<bool>(writer->finish()));

    auto bytes = read_file(out);
    COLLAPSAR_REQUIRE(bytes.size() >= 22);

    // First 4 bytes must be the LFH signature.
    COLLAPSAR_REQUIRE_EQ(read32_le(bytes.data()), zip::LFH_SIGNATURE);

    // Last 22 bytes must start with the EOCD signature.
    const std::byte* eocd = bytes.data() + bytes.size() - 22;
    COLLAPSAR_REQUIRE_EQ(read32_le(eocd), zip::EOCD_SIGNATURE);
    COLLAPSAR_REQUIRE_EQ(read16_le(eocd + 8),  std::uint16_t{1});  // entries on this disk
    COLLAPSAR_REQUIRE_EQ(read16_le(eocd + 10), std::uint16_t{1});  // total entries
}

void overwrite_flag_is_respected() {
    auto dir = temp_dir();
    auto out = dir / "exists.zip";
    {
        std::ofstream(out, std::ios::binary).put(' ');  // pre-create
    }
    auto fail = make_writer(Format::Zip, out, /*overwrite=*/false);
    COLLAPSAR_REQUIRE(static_cast<bool>(fail));  // construction OK; begin() should fail
    COLLAPSAR_REQUIRE(!fail.value()->begin());

    auto ok = make_writer(Format::Zip, out, /*overwrite=*/true);
    COLLAPSAR_REQUIRE(static_cast<bool>(ok));
    COLLAPSAR_REQUIRE(static_cast<bool>(ok.value()->begin()));
}

} // namespace

int main() {
    return run({
        Case{"writes_a_recognisable_header_and_eocd", writes_a_recognisable_header_and_eocd},
        Case{"overwrite_flag_is_respected",           overwrite_flag_is_respected},
    });
}
