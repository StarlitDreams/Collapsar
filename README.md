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
| `bridge/` | Flat C-ABI shared library (`Collapsar.Bridge.dll`) consumed via P/Invoke. | Windows only |
| `gui/`    | C# + WinUI 3 desktop app (`Collapsar.App`). MVVM, packaged via WinAppSDK. | Windows only |
| `cmake/`  | `FindNVCOMP.cmake` and helpers. | — |
| `docs/`   | Architecture notes. | — |

## Build

### Prerequisites

| Tool | Version | Notes |
|------|---------|-------|
| CMake | 3.26+ | Required for CUDA + presets. |
| C++ compiler | MSVC 19.38+ / clang 17 / gcc 13 | C++20. |
| CUDA Toolkit | 12.3+ | Optional. The build auto-detects and falls back to CPU-only if CUDA or nvCOMP is missing. |
| nvCOMP | 4.0+ | Optional, required only for GPU path. Set `NVCOMP_ROOT` env var. |
| zlib | system or vcpkg | Used by the CPU path and for CRC32. On Windows the build auto-discovers `C:\vcpkg\installed\x64-windows`. |
| .NET SDK | 8.0+ | GUI only. |
| WinAppSDK | 1.5+ | GUI only — restored automatically via NuGet. |

### Linux (core + CLI, GPU optional)

```sh
cmake --preset linux-dev
cmake --build --preset linux-dev
ctest --preset linux-dev
./build/linux-dev/bin/collapsar --help
```

### Windows — CLI + GUI bridge (no GPU)

This is the default Windows preset. It builds the static core library, the
`collapsar.exe` CLI, the `Collapsar.Bridge.dll` shared library that the GUI
P/Invokes into, and copies the runtime DLLs (zlib) next to each executable.

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --test-dir build/windows -C Release --output-on-failure

# Outputs land in build/windows/bin/Release/
#   collapsar.exe
#   Collapsar.Bridge.dll
#   z.dll
#   test_*.exe
```

If you need to use Visual Studio 2022 instead of 2026, swap `windows` for
`windows-vs2022` in both commands.

### Windows — full stack with CUDA + nvCOMP

```powershell
$env:NVCOMP_ROOT = "C:\path\to\nvcomp"   # if not in a standard location
cmake --preset windows-cuda
cmake --build --preset windows-cuda
```

If nvCOMP is missing the configure step prints a warning and silently
downgrades to a CPU-only build — the CLI exe will still be produced.

### Windows — GUI

The GUI references the bridge DLL via copy from the native build directory,
so build the native side first, then:

```powershell
dotnet restore gui\Collapsar.App.csproj
dotnet build  gui\Collapsar.App.csproj -c Release -p:Platform=x64

# Run:
.\gui\bin\x64\Release\net8.0-windows10.0.19041.0\Collapsar.App.exe
```

The csproj copies `Collapsar.Bridge.dll` and `z.dll` from
`build/windows/bin/Release/` (or `build/windows-cuda/bin/Release/` if you
built with CUDA) into the GUI's output directory automatically.

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
