#include "test_harness.hpp"

#include "pipeline/size_router.hpp"

using namespace collapsar;
using collapsar::test::run;
using collapsar::test::Case;

namespace {

void auto_routes_above_threshold_to_gpu_when_available() {
    JobOptions opts;
    opts.algorithm           = Algorithm::Auto;
    opts.gpu_threshold_bytes = 1ull * 1024 * 1024;  // 1 MiB

    SizeRouter router{opts, /*gpu_available=*/true};
    COLLAPSAR_REQUIRE(router.route(2 * 1024 * 1024) == SizeRouter::Decision::Gpu);
    COLLAPSAR_REQUIRE(router.route(512 * 1024)      == SizeRouter::Decision::Cpu);
}

void auto_falls_back_to_cpu_when_gpu_unavailable() {
    JobOptions opts;
    opts.algorithm = Algorithm::Auto;
    SizeRouter router{opts, /*gpu_available=*/false};
    COLLAPSAR_REQUIRE(router.route(1ull << 30) == SizeRouter::Decision::Cpu);
}

void explicit_cpu_overrides_size() {
    JobOptions opts;
    opts.algorithm = Algorithm::CpuDeflate;
    SizeRouter router{opts, /*gpu_available=*/true};
    COLLAPSAR_REQUIRE(router.route(1ull << 30) == SizeRouter::Decision::Cpu);
}

void explicit_gpu_routes_small_files_to_gpu() {
    JobOptions opts;
    opts.algorithm = Algorithm::GpuDeflate;
    SizeRouter router{opts, /*gpu_available=*/true};
    COLLAPSAR_REQUIRE(router.route(0) == SizeRouter::Decision::Gpu);
    COLLAPSAR_REQUIRE(router.route(1) == SizeRouter::Decision::Gpu);
}

void boundary_at_threshold_is_gpu() {
    JobOptions opts;
    opts.algorithm           = Algorithm::Auto;
    opts.gpu_threshold_bytes = 4096;
    SizeRouter router{opts, /*gpu_available=*/true};
    COLLAPSAR_REQUIRE(router.route(4096) == SizeRouter::Decision::Gpu);
    COLLAPSAR_REQUIRE(router.route(4095) == SizeRouter::Decision::Cpu);
}

} // namespace

int main() {
    return run({
        Case{"auto_routes_above_threshold_to_gpu_when_available", auto_routes_above_threshold_to_gpu_when_available},
        Case{"auto_falls_back_to_cpu_when_gpu_unavailable",      auto_falls_back_to_cpu_when_gpu_unavailable},
        Case{"explicit_cpu_overrides_size",                       explicit_cpu_overrides_size},
        Case{"explicit_gpu_routes_small_files_to_gpu",            explicit_gpu_routes_small_files_to_gpu},
        Case{"boundary_at_threshold_is_gpu",                      boundary_at_threshold_is_gpu},
    });
}
