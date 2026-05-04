#include "collapsar/capabilities.hpp"

#include "collapsar/config.hpp"

#if COLLAPSAR_HAS_CUDA
#  include <cuda_runtime.h>
#endif

namespace collapsar {

Capabilities probe_capabilities() {
    Capabilities caps;
    // CPU codecs are always available.
    caps.supported.push_back(Algorithm::CpuDeflate);

#if COLLAPSAR_HAS_CUDA
    int count = 0;
    if (cudaGetDeviceCount(&count) == cudaSuccess && count > 0) {
        caps.has_cuda          = true;
        caps.cuda_device_count = count;
        caps.cuda_device_names.reserve(static_cast<std::size_t>(count));
        for (int i = 0; i < count; ++i) {
            cudaDeviceProp prop{};
            if (cudaGetDeviceProperties(&prop, i) == cudaSuccess) {
                caps.cuda_device_names.emplace_back(prop.name);
            } else {
                caps.cuda_device_names.emplace_back("unknown");
            }
        }
        caps.supported.push_back(Algorithm::GpuDeflate);
        caps.supported.push_back(Algorithm::GpuGDeflate);
    }
#endif

    return caps;
}

} // namespace collapsar
