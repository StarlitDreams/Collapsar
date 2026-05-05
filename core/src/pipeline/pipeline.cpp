#include "pipeline.hpp"

#include <algorithm>
#include <chrono>
#include <future>
#include <numeric>
#include <utility>

#include "../codec/codec_factory.hpp"
#include "../codec/crc32.hpp"
#include "../format/i_writer.hpp"
#include "../io/async_reader.hpp"
#include "../io/file_scanner.hpp"
#include "../util/log.hpp"
#include "size_router.hpp"

namespace collapsar {
namespace {

// Pulls the FileBlocks out of their futures and runs them through the chosen
// codec, returning a fully assembled CompressedBlock list in source order.
//
// CPU path: each block is compressed inline on the calling thread (which is
// itself a CPU pool worker, so the parallelism is in "many files in flight").
// GPU path: blocks are accumulated and submitted as one batched call.
template <typename Fn>
[[nodiscard]] Result<std::vector<CompressedBlock>> drain_and_compress(
    std::vector<std::future<Result<FileBlock>>> block_futures,
    Fn&&                                         compress_one,
    std::stop_token                              stop)
{
    std::vector<CompressedBlock> out;
    out.reserve(block_futures.size());

    for (auto& fut : block_futures) {
        if (stop.stop_requested()) {
            return make_error(StatusCode::Cancelled, "compression cancelled");
        }

        auto block_res = fut.get();
        if (!block_res) return std::move(block_res).error();
        FileBlock block = std::move(block_res).value();

        CompressedBlock compressed{
            .file_id        = block.file_id,
            .block_index    = block.block_index,
            .is_last        = block.is_last,
            .logical_offset = block.logical_offset,
            .bytes          = {},
            .crc32          = 0,
            .uncompressed   = 0,
        };

        PlainBlock plain{
            .file_id        = block.file_id,
            .block_index    = block.block_index,
            .is_last        = block.is_last,
            .logical_offset = block.logical_offset,
            .source         = std::span<const std::byte>{block.payload.data(), block.used_bytes},
        };

        auto compress_res = compress_one(plain, compressed);
        if (!compress_res) return std::move(compress_res).error();

        out.push_back(std::move(compressed));
    }

    return out;
}

[[nodiscard]] PreparedFile assemble(FileEntry entry, std::vector<CompressedBlock> blocks) {
    // Stitch per-block CRCs into the file CRC. Blocks arrive in source order
    // already because we drained the futures sequentially.
    std::uint32_t crc          = 0;
    std::uint64_t uncompressed = 0;
    std::uint64_t compressed   = 0;
    for (const auto& b : blocks) {
        crc          = (uncompressed == 0) ? b.crc32
                                            : Crc32::combine(crc, b.crc32, b.uncompressed);
        uncompressed += b.uncompressed;
        compressed   += b.bytes.size();
    }

    return PreparedFile{
        .entry        = std::move(entry),
        .blocks       = std::move(blocks),
        .file_crc32   = crc,
        .uncompressed = uncompressed,
        .compressed   = compressed,
    };
}

class ProgressCoalescer {
public:
    explicit ProgressCoalescer(ProgressCallback cb,
                                std::size_t      total_files,
                                std::size_t      total_bytes,
                                std::chrono::steady_clock::time_point start)
        : cb_{std::move(cb)}, start_{start} {
        snapshot_.total_files = total_files;
        snapshot_.total_bytes = total_bytes;
    }

    void on_file_started(const std::filesystem::path& current) {
        snapshot_.current_file = current;
    }

    void on_file_completed(std::size_t in_bytes, std::size_t out_bytes) {
        snapshot_.processed_files += 1;
        snapshot_.processed_bytes += in_bytes;
        snapshot_.output_bytes    += out_bytes;
        maybe_emit();
    }

    void flush() {
        if (cb_) {
            const auto elapsed = std::chrono::steady_clock::now() - start_;
            const auto secs    = std::chrono::duration<double>(elapsed).count();
            snapshot_.throughput_mibps = secs > 0
                ? (static_cast<double>(snapshot_.processed_bytes) / (1024.0 * 1024.0)) / secs
                : 0.0;
            cb_(snapshot_);
        }
    }

private:
    void maybe_emit() {
        const auto now = std::chrono::steady_clock::now();
        if (now - last_emit_ < std::chrono::milliseconds{50}) return;
        last_emit_ = now;
        flush();
    }

    ProgressCallback                         cb_;
    ProgressEvent                            snapshot_;
    std::chrono::steady_clock::time_point    start_;
    std::chrono::steady_clock::time_point    last_emit_{};
};

} // namespace

Pipeline::Pipeline(EngineOptions engine, ThreadPool& cpu_pool, ThreadPool& io_pool)
    : engine_{engine}, cpu_pool_{cpu_pool}, io_pool_{io_pool} {}

Result<JobStats> Pipeline::run(JobRequest req, std::stop_token stop, ProgressCallback on_event) {
    const auto t_start = std::chrono::steady_clock::now();

    // ---- Scan & validate ---------------------------------------------------
    auto entries_res = scan(req.inputs, req.options.recurse);
    if (!entries_res) return std::move(entries_res).error();
    auto entries = std::move(entries_res).value();
    if (entries.empty()) {
        return make_error(StatusCode::InvalidArgument, "no input files");
    }

    // ---- Codecs & writer ---------------------------------------------------
    auto codecs_res = make_codecs(engine_, req.options);
    if (!codecs_res) return std::move(codecs_res).error();
    auto codecs = std::move(codecs_res).value();

    const bool gpu_available = static_cast<bool>(codecs.gpu);
    SizeRouter router{req.options, gpu_available};

    auto writer_res = make_writer(req.options.format, req.output, req.options.overwrite);
    if (!writer_res) return std::move(writer_res).error();
    auto writer = std::move(writer_res).value();
    if (auto r = writer->begin(); !r) return r.error();

    AsyncReader reader{io_pool_, req.options.block_bytes};

    // ---- Pre-compute totals for progress -----------------------------------
    const std::size_t total_files = entries.size();
    const std::size_t total_bytes = std::accumulate(
        entries.begin(), entries.end(), std::size_t{0},
        [](std::size_t acc, const FileEntry& e) { return acc + static_cast<std::size_t>(e.size_bytes); });

    ProgressCoalescer progress{std::move(on_event), total_files, total_bytes, t_start};

    JobStats stats{};

    // ---- Per-file processing ----------------------------------------------
    //
    // We process files in input order: each file is a job that runs read +
    // compress + assemble on a worker thread. The writer drains the resulting
    // futures in order, so the archive is deterministic. Multiple files can
    // be in-flight at once, bounded by the CPU pool's queue depth.
    //
    // GPU work serialises on `gpu_mutex` (the GPU codec keeps internal stream
    // pipelining; the mutex just stops two host threads from racing on it).

    std::mutex gpu_mutex;

    std::vector<std::future<Result<PreparedFile>>> file_futures;
    file_futures.reserve(entries.size());

    for (auto& entry : entries) {
        if (stop.stop_requested()) {
            return make_error(StatusCode::Cancelled, "job cancelled");
        }

        const SizeRouter::Decision route = router.route(entry.size_bytes);
        const bool                  use_gpu = (route == SizeRouter::Decision::Gpu);

        auto block_futs_res = reader.submit(entry, stop);
        if (!block_futs_res) return std::move(block_futs_res).error();
        // Wrap the futures in a shared_ptr so the per-file task lambda stays
        // trivially copyable. std::future is move-only, and MSVC's thread-pool
        // plumbing (std::function / std::packaged_task internals) instantiates
        // copy paths even when only move semantics are exercised at runtime.
        auto futs_ptr = std::make_shared<std::vector<std::future<Result<FileBlock>>>>(
            std::move(block_futs_res).value());

        file_futures.emplace_back(cpu_pool_.submit(
            [entry, futs_ptr, use_gpu, stop,
             cpu = codecs.cpu.get(), gpu = codecs.gpu.get(), &gpu_mutex]() mutable
            -> Result<PreparedFile> {
                auto& futs = *futs_ptr;
                Result<std::vector<CompressedBlock>> blocks_res = make_error(StatusCode::Internal, "unset");
                if (use_gpu) {
                    // Simple per-file batching: collect every block in pinned
                    // host memory, hand them to the GPU codec as one batch.
                    std::vector<FileBlock> drained;
                    drained.reserve(futs.size());
                    for (auto& f : futs) {
                        if (stop.stop_requested()) {
                            return make_error(StatusCode::Cancelled, "gpu job cancelled");
                        }
                        auto r = f.get();
                        if (!r) return std::move(r).error();
                        drained.push_back(std::move(r).value());
                    }

                    std::vector<PlainBlock>      plains;
                    std::vector<CompressedBlock> outs;
                    plains.reserve(drained.size());
                    outs.reserve(drained.size());
                    for (const auto& b : drained) {
                        plains.push_back(PlainBlock{
                            .file_id        = b.file_id,
                            .block_index    = b.block_index,
                            .is_last        = b.is_last,
                            .logical_offset = b.logical_offset,
                            .source         = std::span<const std::byte>{b.payload.data(), b.used_bytes},
                        });
                        outs.push_back(CompressedBlock{
                            .file_id        = b.file_id,
                            .block_index    = b.block_index,
                            .is_last        = b.is_last,
                            .logical_offset = b.logical_offset,
                            .bytes          = {},
                            .crc32          = 0,
                            .uncompressed   = 0,
                        });
                    }

                    {
                        std::scoped_lock lock{gpu_mutex};
                        auto r = gpu->compress_batch(plains, outs);
                        if (!r) return r.error();
                    }
                    blocks_res = std::move(outs);
                } else {
                    blocks_res = drain_and_compress(
                        std::move(futs),
                        [cpu](const PlainBlock& in, CompressedBlock& out) {
                            return cpu->compress(in, out);
                        },
                        stop);
                }
                if (!blocks_res) return std::move(blocks_res).error();
                return assemble(std::move(entry), std::move(blocks_res).value());
            }));
    }

    // ---- Drain in input order, hand to writer ------------------------------
    for (auto& fut : file_futures) {
        if (stop.stop_requested()) {
            return make_error(StatusCode::Cancelled, "job cancelled");
        }
        auto file_res = fut.get();
        if (!file_res) return std::move(file_res).error();
        auto prepared = std::move(file_res).value();

        progress.on_file_started(prepared.entry.archive_path);

        const auto in_bytes  = static_cast<std::size_t>(prepared.uncompressed);
        const auto out_bytes = static_cast<std::size_t>(prepared.compressed);

        // Per-file routing tally for diagnostics.
        const auto route = router.route(prepared.entry.size_bytes);
        if (route == SizeRouter::Decision::Gpu) {
            stats.gpu_blocks += prepared.blocks.size();
        } else {
            stats.cpu_blocks += prepared.blocks.size();
        }

        if (auto r = writer->write_file(std::move(prepared)); !r) return r.error();

        stats.files_processed += 1;
        stats.input_bytes      += in_bytes;
        stats.output_bytes     += out_bytes;
        progress.on_file_completed(in_bytes, out_bytes);
    }

    if (auto r = writer->finish(); !r) return r.error();

    stats.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t_start);
    progress.flush();
    return stats;
}

} // namespace collapsar
