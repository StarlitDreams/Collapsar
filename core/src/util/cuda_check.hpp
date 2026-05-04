#pragma once

#include "collapsar/config.hpp"

#if COLLAPSAR_HAS_CUDA

#include <cuda_runtime.h>
#include <nvcomp.hpp>

#include <sstream>
#include <string>

#include "collapsar/error.hpp"

namespace collapsar::cuda {

[[nodiscard]] inline Error from_cuda(cudaError_t status, const char* expr, const char* file, int line) {
    std::ostringstream oss;
    oss << "CUDA error " << static_cast<int>(status)
        << " (" << cudaGetErrorName(status) << "): " << cudaGetErrorString(status)
        << " at " << file << ':' << line << " — " << expr;
    return make_error(StatusCode::CudaError, oss.str());
}

[[nodiscard]] inline Error from_nvcomp(nvcompStatus_t status, const char* expr, const char* file, int line) {
    std::ostringstream oss;
    oss << "nvCOMP error " << static_cast<int>(status)
        << " at " << file << ':' << line << " — " << expr;
    return make_error(StatusCode::NvcompError, oss.str());
}

} // namespace collapsar::cuda

// Evaluate `expr` and, on failure, return a Result<>-compatible Error from the
// enclosing function. Use only inside functions that return Result<T>.
#define COLLAPSAR_CUDA_TRY(expr)                                                     \
    do {                                                                             \
        const cudaError_t _status_ = (expr);                                         \
        if (_status_ != cudaSuccess) {                                               \
            return ::collapsar::cuda::from_cuda(_status_, #expr, __FILE__, __LINE__);\
        }                                                                            \
    } while (0)

#define COLLAPSAR_NVCOMP_TRY(expr)                                                       \
    do {                                                                                 \
        const nvcompStatus_t _status_ = (expr);                                          \
        if (_status_ != nvcompSuccess) {                                                 \
            return ::collapsar::cuda::from_nvcomp(_status_, #expr, __FILE__, __LINE__);  \
        }                                                                                \
    } while (0)

#endif // COLLAPSAR_HAS_CUDA
