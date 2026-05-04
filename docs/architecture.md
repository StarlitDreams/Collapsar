# Collapsar architecture

This is the design write-up. The README has the user-facing version; this is
for someone reading or modifying the code.

## Layered view

```
┌─────────────────────────────────────────────────────────┐
│  WinUI 3 GUI (gui/)                                     │
│  MainViewModel ────────► PackAsync(JobOptions, files)   │
└────────────────────────────────│────────────────────────┘
                                 │ managed
                                 ▼
┌─────────────────────────────────────────────────────────┐
│  Collapsar.Bridge (bridge/, C++/CLI, Windows-only)       │
│  Marshals strings/enums, owns one collapsar::Engine.    │
└────────────────────────────────│────────────────────────┘
                                 │ native
                                 ▼
┌─────────────────────────────────────────────────────────┐
│  collapsar_core (core/, C++20 + CUDA)                   │
│                                                         │
│   Engine ──► Pipeline ──► Codecs ──► Writer             │
│                  │                                      │
│                  ├── ThreadPool (CPU codec)             │
│                  ├── ThreadPool (I/O)                   │
│                  └── CUDA streams (GPU codec)           │
└─────────────────────────────────────────────────────────┘
```

The bridge is a thin shell. Everything material lives in `collapsar_core`,
and the `cli/` driver is a faithful replica of what the GUI does — which is
why the CLI is the primary surface for development and CI.

## Pipeline

```
   FileScanner ──► AsyncReader (io_pool) ──► SizeRouter
                                                │
                          ┌─────────────────────┴──────────────────┐
                          ▼                                         ▼
              cpu_pool task (per file)                 cpu_pool task (per file)
                          │                                         │
            zlib deflate per block                  nvCOMP batched Deflate
                          │                              (CUDA streams)
                          └────────────► Reorder buffer ◄────────────┘
                                              │
                                              ▼
                                       IWriter (ZIP/Gzip)
```

The pipeline runs *one job* at a time inside an Engine, but parallelises
within a job along three axes:

1. **Across files** — multiple files are in flight simultaneously. Each file
   becomes one task on the CPU thread pool that owns the file's read/compress
   chain end-to-end.
2. **Across blocks of one file** — large files are sliced into `block_bytes`
   pieces (default 8 MiB). Reads run on the I/O pool; the per-file task drains
   the resulting block futures in source order.
3. **Across stages** — file *N+1* can be reading while file *N* is in the
   compressor and file *N-1* is being written. This works because the file
   tasks are independent and the writer drains their results in submission
   order from a vector of futures.

### Bounded backpressure

Each thread pool has a queue. The `cpu_pool` is sized to `hardware_concurrency`
by default; the `io_pool` to 4. There's no explicit semaphore — submitting
a million file futures up front would just enqueue them, and the worst case
is bounded by however many files the user actually selected. For
production-grade memory bounding we'd add a `max_inflight_files` counter on
the producer side; the current code is fine for tens of thousands of inputs.

### CPU vs GPU routing

`SizeRouter` is a pure function of `(file_size, algorithm, gpu_available)`:

| `algorithm`        | `gpu_available` | Result                                |
|--------------------|-----------------|---------------------------------------|
| `Auto`             | `true`          | GPU iff `file_size >= gpu_threshold`  |
| `Auto`             | `false`         | CPU                                   |
| `CpuDeflate`       | any             | CPU                                   |
| `GpuDeflate`/`Gpu*`| `true`          | GPU                                   |
| `GpuDeflate`/`Gpu*`| `false`         | (configurable error / fallback — current default is to fail open in `make_codecs`) |

The threshold (default 1 MiB) is the cross-over point on a typical desktop
configuration: PCIe 4.0 x16 transfer plus nvCOMP launch overhead beats
zlib-ng on a single block somewhere around the megabyte mark.

### GPU stream pipelining

`NvcompDeflateCodec` owns a small ring of CUDA streams (default 3). Each slot
in the ring contains:

- A non-blocking `cudaStream_t`
- A device-side input staging buffer
- A device-side output staging buffer
- A device-side temp buffer for nvCOMP's working memory
- An event used to gate the next reuse of the slot

Per batch:

1. Wait on the slot's event (so the previous use finished)
2. `cudaMemcpyAsync` H2D for every chunk
3. Push pointer/size arrays H2D
4. `nvcompBatchedDeflateCompressAsync`
5. `cudaMemcpyAsync` size-array D2H + sync (we need the actual sizes before D2H'ing payloads)
6. Per-chunk D2H of the compressed payloads
7. Record the event
8. Sync the stream

Steps 1-6 of the *next* batch can run while step 7's previous-slot work is
still draining, which is what hides the PCIe round-trip behind the next
compute kernel. The pipeline already serialises GPU calls through a host-side
mutex (`gpu_mutex` in `pipeline.cpp`), so the codec sees one batch at a time —
the overlap happens *inside* the codec via the slot ring.

## Format choices

ZIP only embeds **standard deflate** (RFC 1951) — that's why we use nvCOMP's
`nvcompBatchedDeflate` API rather than its faster but proprietary GDeflate.
A future "Collapsar Fast" format could ship GDeflate output as a non-portable
container; that's deliberately not the default because the entire point of
producing a `.zip` is portability.

The ZIP writer is hand-rolled (no `libzip`, no `minizip`):

- Local file headers + payload are streamed as each file completes
- Central directory entries are accumulated in memory
- Final `finish()` writes the central directory + EOCD
- Zip64 promotion is automatic for individual files >= 4 GiB or archives
  with more than 65535 entries / total size >= 4 GiB

CRC32 stays on the host. nvCOMP doesn't return a CRC, and computing it
host-side from pinned memory is dominated by the I/O cost — adding a custom
CUDA kernel for CRC saves wall time only when the source is *already*
device-resident, which is not our case.

## Error handling and cancellation

- Public API never throws. Everything returns `Result<T>` (a tagged union of
  `T` and `Error`). The bridge translates `Error` into a managed `JobResult`.
- Inside a job, errors propagate by short-circuiting the per-file futures —
  the first file whose task returns an `Error` causes the writer-drain loop
  to bail. Previously-written entries are flushed; the partial output file
  is left on disk so the user can decide what to do with it (a future
  improvement: delete on first error).
- Cancellation: `JobHandle::cancel()` flips a `std::stop_source`. Workers
  observe the token at every queue boundary and on each block in the
  drain-and-compress loop. The first observation returns `StatusCode::Cancelled`
  upstream and the rest of the pipeline winds down naturally.

## Why no exceptions across thread boundaries?

The pipeline runs on `cpu_pool` workers. If a worker throws, the exception
goes into a `std::packaged_task`'s future and re-throws on `future::get`,
which would cross a CUDA boundary in the GPU path and a `std::async` boundary
in the engine. Returning `Result<T>` instead keeps the failure in plain data
and means the bridge can hand a managed `StatusCode` back to C# without ever
having to swallow a native exception.

## What's *not* here yet

- A global GPU work queue — currently each per-file task batches on its own,
  so files smaller than `gpu_batch_bytes` are submitted as small batches.
  The `compress_batch` API is already shaped for cross-file batching; a
  dispatcher thread can be added without API changes.
- `zstd` writer (the format enum exists but `make_writer` returns
  `Unsupported`).
- ZIP data-descriptor / streaming-CRC mode for files larger than memory.
  The current writer streams each file's compressed bytes directly to disk
  but builds the LFH up-front using the fully-known size & CRC; this is
  correct for arbitrary file sizes because we accumulate per-block CRCs into
  a file CRC before the LFH is emitted.
- Overlapped I/O on Windows — currently `std::ifstream`, which is fine for
  the prototype but a `ReadFileEx` / `io_uring` abstraction would beat it
  on NVMe drives.
