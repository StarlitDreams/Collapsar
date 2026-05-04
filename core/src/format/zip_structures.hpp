#pragma once

#include <cstdint>

namespace collapsar::zip {

// Signature constants — see APPNOTE.TXT 4.3.7+.
inline constexpr std::uint32_t LFH_SIGNATURE              = 0x04034b50;
inline constexpr std::uint32_t CDH_SIGNATURE              = 0x02014b50;
inline constexpr std::uint32_t EOCD_SIGNATURE             = 0x06054b50;
inline constexpr std::uint32_t ZIP64_EOCD_SIGNATURE       = 0x06064b50;
inline constexpr std::uint32_t ZIP64_EOCD_LOCATOR_SIG     = 0x07064b50;
inline constexpr std::uint32_t DATA_DESCRIPTOR_SIGNATURE  = 0x08074b50;

// Compression methods.
inline constexpr std::uint16_t METHOD_STORE   = 0;
inline constexpr std::uint16_t METHOD_DEFLATE = 8;

// Extra field header IDs.
inline constexpr std::uint16_t EXTRA_ID_ZIP64 = 0x0001;

// Sentinel used when a 32-bit field overflows and the real value lives in a
// Zip64 extra field.
inline constexpr std::uint32_t U32_MAX = 0xFFFFFFFFu;
inline constexpr std::uint16_t U16_MAX = 0xFFFFu;

// Version-needed-to-extract values.
inline constexpr std::uint16_t VERSION_DEFLATE = 20;
inline constexpr std::uint16_t VERSION_ZIP64   = 45;

} // namespace collapsar::zip
