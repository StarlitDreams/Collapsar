#include "file_scanner.hpp"

#include <algorithm>
#include <atomic>
#include <system_error>

namespace collapsar {
namespace {

namespace fs = std::filesystem;

std::atomic<std::uint64_t> g_next_id{1};

fs::path archive_path_for(const fs::path& root, const fs::path& file) {
    // For "data/sub/foo.bin" with root "data", the archive path is "sub/foo.bin".
    auto rel = fs::relative(file, root);
    return rel.empty() ? file.filename() : rel;
}

void scan_directory(const fs::path& root, bool recurse, std::vector<FileEntry>& out, std::error_code& ec) {
    auto handle_entry = [&](const fs::directory_entry& de) {
        if (!de.is_regular_file(ec)) return;
        FileEntry entry{
            .id           = g_next_id.fetch_add(1, std::memory_order_relaxed),
            .source       = fs::absolute(de.path(), ec),
            .archive_path = archive_path_for(root.parent_path().empty() ? root : root.parent_path(), de.path()),
            .size_bytes   = de.file_size(ec),
            .mtime        = de.last_write_time(ec),
        };
        out.push_back(std::move(entry));
    };

    if (recurse) {
        for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::recursive_directory_iterator{};
             it.increment(ec)) {
            handle_entry(*it);
        }
    } else {
        for (auto it = fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::directory_iterator{};
             it.increment(ec)) {
            handle_entry(*it);
        }
    }
}

} // namespace

Result<std::vector<FileEntry>> scan(const std::vector<fs::path>& inputs, bool recurse) {
    std::vector<FileEntry> entries;
    entries.reserve(inputs.size());
    std::error_code ec;

    for (const auto& input : inputs) {
        if (!fs::exists(input, ec) || ec) {
            return make_error(StatusCode::NotFound, "Input does not exist: " + input.string());
        }

        if (fs::is_regular_file(input, ec)) {
            FileEntry entry{
                .id           = g_next_id.fetch_add(1, std::memory_order_relaxed),
                .source       = fs::absolute(input, ec),
                .archive_path = input.filename(),
                .size_bytes   = fs::file_size(input, ec),
                .mtime        = fs::last_write_time(input, ec),
            };
            if (ec) {
                return make_error(StatusCode::Io, "Failed to stat: " + input.string() + " — " + ec.message());
            }
            entries.push_back(std::move(entry));
            continue;
        }

        if (fs::is_directory(input, ec)) {
            const auto before = entries.size();
            scan_directory(input, recurse, entries, ec);
            if (ec) {
                return make_error(StatusCode::Io, "Directory walk failed for " + input.string() + " — " + ec.message());
            }
            // Sort the slice we just appended for deterministic ordering.
            std::sort(entries.begin() + static_cast<std::ptrdiff_t>(before), entries.end(),
                      [](const FileEntry& a, const FileEntry& b) {
                          return a.archive_path < b.archive_path;
                      });
            continue;
        }

        return make_error(StatusCode::InvalidArgument, "Unsupported input type: " + input.string());
    }

    return entries;
}

} // namespace collapsar
