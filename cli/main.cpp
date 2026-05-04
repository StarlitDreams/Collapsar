// Collapsar command-line driver.
//
// Usage:
//   collapsar pack --output PATH [options] FILE [FILE ...]
//   collapsar info
//
// Useful as a smoke-test for the engine and as the reference consumer of the
// public API. The Windows GUI calls into the same API via a C++/CLI bridge.

#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#include "collapsar/engine.hpp"

namespace {

void print_help() {
    std::cout <<
        "Collapsar — GPU-accelerated compression\n"
        "\n"
        "Usage:\n"
        "  collapsar pack --output PATH [options] FILE [FILE ...]\n"
        "  collapsar info\n"
        "  collapsar --help\n"
        "\n"
        "Options for `pack`:\n"
        "  --output PATH         Output archive path (required)\n"
        "  --format {zip|gzip}   Output format (default: zip)\n"
        "  --gpu / --no-gpu      Force GPU on/off (default: on if available)\n"
        "  --gpu-threshold N     Bytes; files smaller than N stay on the CPU path (default: 1048576)\n"
        "  --block-bytes N       Per-block read size (default: 8388608)\n"
        "  --threads N           CPU compression worker count (default: hardware concurrency)\n"
        "  --io-threads N        I/O worker count (default: 4)\n"
        "  --level {fast|balanced|best}  CPU codec level (default: balanced)\n"
        "  --overwrite           Replace OUTPUT if it already exists\n"
        "  --no-recurse          Don't descend into directories\n"
        "  --quiet               Suppress per-file progress\n";
}

[[nodiscard]] bool parse_size(std::string_view s, std::size_t& out) {
    std::size_t multiplier = 1;
    if (!s.empty()) {
        const char tail = static_cast<char>(s.back());
        switch (tail) {
            case 'K': case 'k': multiplier = 1ull << 10; s.remove_suffix(1); break;
            case 'M': case 'm': multiplier = 1ull << 20; s.remove_suffix(1); break;
            case 'G': case 'g': multiplier = 1ull << 30; s.remove_suffix(1); break;
            default: break;
        }
    }
    std::uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
    if (ec != std::errc{} || ptr != s.data() + s.size()) return false;
    out = static_cast<std::size_t>(value * multiplier);
    return true;
}

int cmd_info() {
    auto caps = collapsar::probe_capabilities();
    std::cout << "CUDA available    : " << (caps.has_cuda ? "yes" : "no") << '\n';
    std::cout << "CUDA device count : " << caps.cuda_device_count << '\n';
    for (std::size_t i = 0; i < caps.cuda_device_names.size(); ++i) {
        std::cout << "  [" << i << "] " << caps.cuda_device_names[i] << '\n';
    }
    std::cout << "Algorithms        :";
    for (auto a : caps.supported) {
        switch (a) {
            case collapsar::Algorithm::CpuDeflate:  std::cout << " cpu-deflate"; break;
            case collapsar::Algorithm::GpuDeflate:  std::cout << " gpu-deflate"; break;
            case collapsar::Algorithm::GpuGDeflate: std::cout << " gpu-gdeflate"; break;
            case collapsar::Algorithm::CpuZstd:     std::cout << " cpu-zstd"; break;
            case collapsar::Algorithm::GpuZstd:     std::cout << " gpu-zstd"; break;
            case collapsar::Algorithm::Auto:        std::cout << " auto"; break;
        }
    }
    std::cout << '\n';
    return 0;
}

struct PackArgs {
    std::vector<std::filesystem::path> inputs;
    std::filesystem::path              output;
    collapsar::Format                  format     = collapsar::Format::Zip;
    bool                               use_gpu    = true;
    bool                               gpu_set    = false;
    std::size_t                        gpu_threshold = 1ull * 1024 * 1024;
    std::size_t                        block_bytes   = 8ull * 1024 * 1024;
    std::size_t                        threads       = 0;  // 0 → auto
    std::size_t                        io_threads    = 4;
    collapsar::Level                   level         = collapsar::Level::Balanced;
    bool                               overwrite     = false;
    bool                               recurse       = true;
    bool                               quiet         = false;
};

[[nodiscard]] bool parse_pack(int argc, char** argv, PackArgs& args, std::string& err) {
    for (int i = 2; i < argc; ++i) {
        std::string_view a = argv[i];
        auto need_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { err = std::string{"missing value for "} + name; return nullptr; }
            return argv[++i];
        };

        if (a == "--output") {
            const char* v = need_value("--output"); if (!v) return false;
            args.output = v;
        } else if (a == "--format") {
            const char* v = need_value("--format"); if (!v) return false;
            std::string_view fmt = v;
            if      (fmt == "zip")  args.format = collapsar::Format::Zip;
            else if (fmt == "gzip") args.format = collapsar::Format::Gzip;
            else if (fmt == "zstd") args.format = collapsar::Format::Zstd;
            else { err = "unknown --format value"; return false; }
        } else if (a == "--gpu") {
            args.use_gpu = true; args.gpu_set = true;
        } else if (a == "--no-gpu") {
            args.use_gpu = false; args.gpu_set = true;
        } else if (a == "--gpu-threshold") {
            const char* v = need_value("--gpu-threshold"); if (!v) return false;
            if (!parse_size(v, args.gpu_threshold)) { err = "invalid --gpu-threshold"; return false; }
        } else if (a == "--block-bytes") {
            const char* v = need_value("--block-bytes"); if (!v) return false;
            if (!parse_size(v, args.block_bytes)) { err = "invalid --block-bytes"; return false; }
        } else if (a == "--threads") {
            const char* v = need_value("--threads"); if (!v) return false;
            if (!parse_size(v, args.threads)) { err = "invalid --threads"; return false; }
        } else if (a == "--io-threads") {
            const char* v = need_value("--io-threads"); if (!v) return false;
            if (!parse_size(v, args.io_threads)) { err = "invalid --io-threads"; return false; }
        } else if (a == "--level") {
            const char* v = need_value("--level"); if (!v) return false;
            std::string_view lv = v;
            if      (lv == "fast")     args.level = collapsar::Level::Fast;
            else if (lv == "balanced") args.level = collapsar::Level::Balanced;
            else if (lv == "best")     args.level = collapsar::Level::Best;
            else { err = "unknown --level value"; return false; }
        } else if (a == "--overwrite")  { args.overwrite = true;  }
        else if   (a == "--no-recurse") { args.recurse   = false; }
        else if   (a == "--quiet")      { args.quiet     = true;  }
        else if   (a == "--help" || a == "-h") { print_help(); return false; }
        else if   (a.size() > 2 && a.substr(0, 2) == "--") {
            err = "unknown option: " + std::string{a};
            return false;
        } else {
            args.inputs.emplace_back(a);
        }
    }

    if (args.output.empty()) { err = "--output is required"; return false; }
    if (args.inputs.empty()) { err = "at least one input file is required"; return false; }
    return true;
}

int cmd_pack(int argc, char** argv) {
    PackArgs args;
    std::string err;
    if (!parse_pack(argc, argv, args, err)) {
        if (!err.empty()) std::cerr << "collapsar: " << err << '\n';
        return 2;
    }

    collapsar::EngineOptions eopts;
    eopts.io_threads        = args.io_threads;
    if (args.threads > 0) eopts.cpu_codec_threads = args.threads;
    eopts.enable_gpu        = args.use_gpu;

    collapsar::Engine engine{eopts};

    collapsar::JobRequest req;
    req.inputs            = args.inputs;
    req.output            = args.output;
    req.options.format    = args.format;
    req.options.gpu_threshold_bytes = args.gpu_threshold;
    req.options.block_bytes         = args.block_bytes;
    req.options.level     = args.level;
    req.options.overwrite = args.overwrite;
    req.options.recurse   = args.recurse;
    req.options.algorithm = args.gpu_set
        ? (args.use_gpu ? collapsar::Algorithm::Auto : collapsar::Algorithm::CpuDeflate)
        : collapsar::Algorithm::Auto;

    if (!args.quiet) {
        req.on_progress = [](const collapsar::ProgressEvent& e) {
            std::cerr << "\r[" << e.processed_files << "/" << e.total_files << "] "
                      << e.processed_bytes / (1024 * 1024) << " MiB → "
                      << e.output_bytes    / (1024 * 1024) << " MiB  ("
                      << static_cast<int>(e.throughput_mibps) << " MiB/s)        " << std::flush;
        };
    }

    auto res = engine.run(std::move(req));
    if (!args.quiet) std::cerr << '\n';
    if (!res) {
        std::cerr << "collapsar: " << res.error() << '\n';
        return 1;
    }

    const auto& s = res.value();
    std::cout << "Wrote " << s.output_bytes << " bytes from " << s.input_bytes
              << " bytes (" << s.files_processed << " files) in "
              << s.elapsed.count() << " ms\n";
    if (s.input_bytes > 0) {
        const double ratio = static_cast<double>(s.output_bytes) / static_cast<double>(s.input_bytes);
        std::cout << "Compression ratio: " << ratio << " (saved "
                  << (100.0 * (1.0 - ratio)) << "%)\n";
    }
    std::cout << "Routed: " << s.cpu_blocks << " CPU blocks, " << s.gpu_blocks << " GPU blocks\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--help") == 0 || std::strcmp(argv[1], "-h") == 0) {
        print_help();
        return argc < 2 ? 2 : 0;
    }
    const std::string_view cmd = argv[1];
    if (cmd == "info") return cmd_info();
    if (cmd == "pack") return cmd_pack(argc, argv);

    std::cerr << "collapsar: unknown command '" << cmd << "'\n\n";
    print_help();
    return 2;
}
