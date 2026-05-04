# Collapsar

GPU-accelerated file compression for Windows 11. The CPU runs the I/O pipeline
and small-file path; the GPU runs nvCOMP-batched Deflate for everything large
enough that the host-device transfer pays for itself. Output is a standard
`.zip`, `.gz`, or `.zst` file that any common archiver can read.

## Architecture

```
   File Scanner ──► Async Reader (thread pool) ──► Size Router
                                                      │
                            ┌─────────────────────────┴─────────────────┐
                            ▼                                            ▼
                  CPU path (zlib-ng deflate)             GPU path (nvCOMP batched Deflate)
                            │                                            │
                            │                              CUDA pinned host buffers
                            │                              triple-buffered streams
                            │                              H2D ║ compute ║ D2H
                            ▼                                            ▼
                                  Reorder buffer (file-order)
                                                │
                                                ▼
                                  ZIP / GZIP / ZSTD assembler
                                                │
                                                ▼
                                          output.{zip,gz,zst}
```

The pipeline is built from bounded MPMC queues so that each stage exerts
backpressure on the previous one. Cancellation flows from a `std::stop_source`
on the `JobHandle` back through every worker.

## Layout

| Directory | Contents | Builds on |
|-----------|----------|-----------|
| `core/`   | C++20 + CUDA static library (`collapsar_core`). Public headers in `core/include/collapsar/`. | Linux, Windows |
| `cli/`    | `collapsar` command-line driver. Useful for development and CI. | Linux, Windows |
| `bridge/` | C++/CLI assembly (`Collapsar.Bridge.dll`) marshalling the native engine to .NET. | Windows only |
| `gui/`    | C# + WinUI 3 desktop app (`Collapsar.App`). MVVM, packaged via WinAppSDK. | Windows only |
| `cmake/`  | `FindNVCOMP.cmake` and helpers. | — |
| `docs/`   | Architecture notes. | — |

## Build

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | 3.26+ | Required for CUDA + presets. |
| C++ compiler | MSVC 19.38+ / clang 17 / gcc 13 | C++20. |
| CUDA Toolkit | 12.3+ | Optional. Set `-DCOLLAPSAR_WITH_CUDA=OFF` to skip. |
| nvCOMP | 4.0+ | Required when CUDA is enabled. Set `NVCOMP_ROOT` env var. |
| zlib | system or vendored | Used by the CPU path and for CRC32. |
| .NET SDK | 8.0+ | GUI only. |
| WinAppSDK | 1.5+ | GUI only. |

### Linux (core + CLI, GPU optional)

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev
./build/linux-dev/cli/collapsar --help
```

### Windows (full stack)

```powershell
cmake --preset windows-cuda
cmake --build --preset windows-cuda --config Release
# Then open gui/Collapsar.App.sln in Visual Studio 2022 to build the GUI.
```

## Usage (CLI)

```sh
collapsar pack --output archive.zip --format zip --gpu \
    path/to/file_a.bin path/to/file_b.iso path/to/dir/
```

Key flags:

- `--format {zip,gzip,zstd}` — output container.
- `--gpu / --no-gpu` — force GPU on/off.
- `--gpu-threshold 1MiB` — files smaller than this stay on the CPU path.
- `--gpu-batch 256MiB` — accumulate this many bytes per nvCOMP batched call.
- `--level {fast,balanced,best}` — codec level (CPU only; GPU level is fixed by nvCOMP).
- `--threads N` — CPU compression worker count.

## Design notes

See `docs/architecture.md` for the deeper write-up: how stages are wired,
how triple-buffering on CUDA streams overlaps PCIe transfer with compute,
and why the ZIP path uses nvCOMP's standard Deflate codec rather than
GDeflate (GDeflate's bitstream isn't readable by `unzip`).
