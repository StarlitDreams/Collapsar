#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "collapsar/result.hpp"

namespace collapsar {

// One archive entry resolved from the user's input list. We capture the size
// up front so the size-router and progress reporter can pre-compute totals
// without re-stat'ing each file.
struct FileEntry {
    std::uint64_t                  id           = 0;          // assigned by the scanner
    std::filesystem::path          source;                    // absolute path on disk
    std::filesystem::path          archive_path;              // path written into the archive
    std::uint64_t                  size_bytes   = 0;
    std::filesystem::file_time_type mtime{};
};

// Resolves a list of input paths (files or directories) into a flat list of
// FileEntry. Recurses into directories when `recurse` is true. The archive_path
// is computed by stripping the common parent so an input "data/foo" becomes
// "foo" inside the archive.
[[nodiscard]] Result<std::vector<FileEntry>> scan(
    const std::vector<std::filesystem::path>& inputs,
    bool                                       recurse);

} // namespace collapsar
