// End-to-end smoke test: hand the engine a couple of files, get a ZIP out,
// verify the contents decompress to what we put in via zlib.

#include "test_harness.hpp"

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <vector>

#include <zlib.h>

#include "collapsar/engine.hpp"

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

std::vector<std::uint8_t> read_file_bytes(const fs::path& p) {
    std::ifstream is(p, std::ios::binary);
    std::vector<std::uint8_t> out;
    is.seekg(0, std::ios::end);
    out.resize(static_cast<std::size_t>(is.tellg()));
    is.seekg(0);
    is.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(out.size()));
    return out;
}

void write_random(const fs::path& p, std::size_t bytes, std::uint32_t seed) {
    std::mt19937 rng{seed};
    std::ofstream os(p, std::ios::binary);
    std::vector<char> buf(4096);
    std::size_t       written = 0;
    while (written < bytes) {
        for (auto& c : buf) c = static_cast<char>(rng());
        const auto chunk = std::min(buf.size(), bytes - written);
        os.write(buf.data(), static_cast<std::streamsize>(chunk));
        written += chunk;
    }
}

// Inflate a raw deflate stream and compare against expected.
bool inflates_to(const std::uint8_t* compressed, std::size_t comp_size,
                 const std::uint8_t* expected,   std::size_t exp_size) {
    z_stream s{};
    if (inflateInit2(&s, -MAX_WBITS) != Z_OK) return false;

    std::vector<std::uint8_t> out(exp_size);
    s.next_in   = const_cast<Bytef*>(compressed);
    s.avail_in  = static_cast<uInt>(comp_size);
    s.next_out  = out.data();
    s.avail_out = static_cast<uInt>(out.size());

    const int rc = inflate(&s, Z_FINISH);
    inflateEnd(&s);
    if (rc != Z_STREAM_END) return false;
    if (s.total_out != exp_size) return false;
    return std::memcmp(out.data(), expected, exp_size) == 0;
}

void cpu_path_round_trips_a_couple_of_files() {
    auto dir = temp_dir() / "engine_pack";
    fs::remove_all(dir);
    fs::create_directories(dir);

    const auto in_a = dir / "a.bin";
    const auto in_b = dir / "b.bin";
    const auto out  = dir / "archive.zip";

    write_random(in_a, 64 * 1024, 1);
    write_random(in_b, 96 * 1024, 2);

    EngineOptions eopts;
    eopts.enable_gpu = false;            // CPU path only — works on Linux CI without CUDA.
    Engine engine{eopts};

    JobRequest req;
    req.inputs    = {in_a, in_b};
    req.output    = out;
    req.options.format    = Format::Zip;
    req.options.algorithm = Algorithm::CpuDeflate;
    req.options.overwrite = true;

    auto res = engine.run(std::move(req));
    if (!res) std::cerr << "engine.run failed: " << res.error() << '\n';
    COLLAPSAR_REQUIRE(static_cast<bool>(res));

    const auto archive = read_file_bytes(out);
    COLLAPSAR_REQUIRE(archive.size() > 22);

    // Crude inflate check: pull the LFH for entry 0, jump past header+name,
    // and inflate the payload. This isn't a full ZIP parser — just enough to
    // catch regressions where the deflate stream is mangled.
    auto read16 = [](const std::uint8_t* p) {
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    };
    auto read32 = [](const std::uint8_t* p) {
        return static_cast<std::uint32_t>(p[0]) | (std::uint32_t(p[1]) << 8)
             | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
    };

    COLLAPSAR_REQUIRE_EQ(read32(archive.data()), 0x04034b50u);  // PK\003\004
    const std::uint16_t name_len  = read16(archive.data() + 26);
    const std::uint16_t extra_len = read16(archive.data() + 28);
    const std::uint32_t comp_sz   = read32(archive.data() + 18);
    const std::uint32_t uncomp_sz = read32(archive.data() + 22);

    const std::size_t payload_off = 30u + name_len + extra_len;
    const std::vector<std::uint8_t> orig = read_file_bytes(in_a);
    COLLAPSAR_REQUIRE_EQ(uncomp_sz, static_cast<std::uint32_t>(orig.size()));
    COLLAPSAR_REQUIRE(inflates_to(archive.data() + payload_off, comp_sz,
                                   orig.data(), orig.size()));
}

} // namespace

int main() {
    return run({
        Case{"cpu_path_round_trips_a_couple_of_files", cpu_path_round_trips_a_couple_of_files},
    });
}
