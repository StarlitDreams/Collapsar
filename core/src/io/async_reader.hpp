#pragma once

#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <stop_token>

#include "collapsar/result.hpp"
#include "../util/thread_pool.hpp"
#include "file_scanner.hpp"
#include "pinned_buffer.hpp"

namespace collapsar {

// One slice of a file as it flows through the pipeline.
//
// Files larger than `block_bytes` are split across multiple blocks; the writer
// re-orders them on the way out using (file_id, block_index).
struct FileBlock {
    std::uint64_t file_id      = 0;
    std::uint32_t block_index  = 0;
    bool          is_last      = false;
    std::uint64_t logical_offset = 0;          // offset within the source file
    PinnedBuffer  payload;                      // pinned host memory
    std::size_t   used_bytes   = 0;             // bytes actually populated in payload
};

// AsyncReader fans file reads out across a thread pool, emitting FileBlocks via
// std::future. Each future resolves once the corresponding block has been
// populated. Cancellation is observed at block boundaries.
class AsyncReader {
public:
    AsyncReader(ThreadPool& pool, std::size_t block_bytes);

    // Submit a file for reading. Returns one future per block in source order.
    // The reader owns no state about the file beyond the duration of these
    // tasks, so cancelling a future-vector is as simple as dropping it.
    [[nodiscard]] Result<std::vector<std::future<Result<FileBlock>>>> submit(
        const FileEntry& entry,
        std::stop_token  stop);

private:
    ThreadPool& pool_;
    std::size_t block_bytes_;
};

} // namespace collapsar
