#include "crc32.hpp"

#include <zlib.h>

namespace collapsar {

void Crc32::update(std::span<const std::byte> data) noexcept {
    if (data.empty()) return;
    value_ = static_cast<std::uint32_t>(crc32_z(
        value_,
        reinterpret_cast<const Bytef*>(data.data()),
        data.size()));
}

std::uint32_t Crc32::compute(std::span<const std::byte> data) noexcept {
    return static_cast<std::uint32_t>(crc32_z(
        0,
        reinterpret_cast<const Bytef*>(data.data()),
        data.size()));
}

std::uint32_t Crc32::combine(std::uint32_t crc1, std::uint32_t crc2, std::uint64_t len2) noexcept {
    return static_cast<std::uint32_t>(crc32_combine(crc1, crc2, static_cast<z_off_t>(len2)));
}

} // namespace collapsar
