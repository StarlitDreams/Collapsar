#include "test_harness.hpp"

#include <array>
#include <cstring>

#include "codec/crc32.hpp"

using namespace collapsar;
using collapsar::test::run;
using collapsar::test::Case;

namespace {

std::span<const std::byte> bytes_of(std::string_view s) {
    return {reinterpret_cast<const std::byte*>(s.data()), s.size()};
}

void empty_input_is_zero() {
    COLLAPSAR_REQUIRE_EQ(Crc32::compute({}), 0u);
}

void known_vectors() {
    // Standard CRC-32 (IEEE 802.3) test vector — every CRC-32 implementation
    // agrees on this one. Adding more vectors is fine but verify against a
    // reference like `python -c 'import zlib; print(hex(zlib.crc32(b"...")))'`.
    COLLAPSAR_REQUIRE_EQ(Crc32::compute(bytes_of("123456789")), 0xCBF43926u);
}

void incremental_matches_oneshot() {
    const std::string payload = "the quick brown fox jumps over the lazy dog";
    const auto whole = Crc32::compute(bytes_of(payload));

    Crc32 incr;
    incr.update(bytes_of(payload.substr(0, 10)));
    incr.update(bytes_of(payload.substr(10)));
    COLLAPSAR_REQUIRE_EQ(incr.value(), whole);
}

void combine_matches_concatenation() {
    const std::string a = "front-half-of-data";
    const std::string b = "and-the-back-half!";
    const std::string ab = a + b;

    const auto crc_a  = Crc32::compute(bytes_of(a));
    const auto crc_b  = Crc32::compute(bytes_of(b));
    const auto crc_ab = Crc32::compute(bytes_of(ab));

    COLLAPSAR_REQUIRE_EQ(Crc32::combine(crc_a, crc_b, b.size()), crc_ab);
}

} // namespace

int main() {
    return run({
        Case{"empty_input_is_zero",          empty_input_is_zero},
        Case{"known_vectors",                known_vectors},
        Case{"incremental_matches_oneshot",  incremental_matches_oneshot},
        Case{"combine_matches_concatenation", combine_matches_concatenation},
    });
}
