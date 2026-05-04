#include "async_reader.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <utility>

namespace collapsar {

AsyncReader::AsyncReader(ThreadPool& pool, std::size_t block_bytes)
    : pool_{pool}, block_bytes_{block_bytes} {
    if (block_bytes_ == 0) block_bytes_ = 1ull * 1024 * 1024;
}

Result<std::vector<std::future<Result<FileBlock>>>> AsyncReader::submit(
    const FileEntry& entry,
    std::stop_token  stop)
{
    const std::uint64_t total = entry.size_bytes;
    const std::uint32_t block_count = total == 0
        ? 1u
        : static_cast<std::uint32_t>((total + block_bytes_ - 1) / block_bytes_);

    std::vector<std::future<Result<FileBlock>>> futures;
    futures.reserve(block_count);

    for (std::uint32_t idx = 0; idx < block_count; ++idx) {
        const std::uint64_t offset = static_cast<std::uint64_t>(idx) * block_bytes_;
        const std::uint64_t bytes  = std::min<std::uint64_t>(block_bytes_, total - offset);
        const bool          last   = (idx + 1 == block_count);

        futures.emplace_back(pool_.submit(
            [entry, idx, offset, bytes, last, stop]() -> Result<FileBlock> {
                if (stop.stop_requested()) {
                    return make_error(StatusCode::Cancelled, "reader cancelled");
                }

                auto buffer_res = PinnedBuffer::allocate(static_cast<std::size_t>(bytes));
                if (!buffer_res) return std::move(buffer_res).error();
                PinnedBuffer buffer = std::move(buffer_res).value();

                if (bytes > 0) {
                    std::ifstream stream(entry.source, std::ios::binary);
                    if (!stream) {
                        return make_error(StatusCode::Io, "Failed to open " + entry.source.string());
                    }
                    stream.seekg(static_cast<std::streamoff>(offset));
                    stream.read(reinterpret_cast<char*>(buffer.data()),
                                static_cast<std::streamsize>(bytes));
                    if (stream.gcount() != static_cast<std::streamsize>(bytes)) {
                        return make_error(StatusCode::Io,
                                          "Short read on " + entry.source.string()
                                          + " (got " + std::to_string(stream.gcount())
                                          + " of " + std::to_string(bytes) + ")");
                    }
                }

                FileBlock block{
                    .file_id        = entry.id,
                    .block_index    = idx,
                    .is_last        = last,
                    .logical_offset = offset,
                    .payload        = std::move(buffer),
                    .used_bytes     = static_cast<std::size_t>(bytes),
                };
                return block;
            }));
    }

    return futures;
}

} // namespace collapsar
