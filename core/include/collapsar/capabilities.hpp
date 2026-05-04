#pragma once

#include <string>
#include <vector>

#include "collapsar/format.hpp"

namespace collapsar {

// Run-time view of the backends actually compiled in and devices visible at
// process start. The GUI calls this once on launch to enable/disable GPU UI.
struct Capabilities {
    bool                     has_cuda          = false;
    int                      cuda_device_count = 0;
    std::vector<std::string> cuda_device_names;
    std::vector<Algorithm>   supported;
};

[[nodiscard]] Capabilities probe_capabilities();

} // namespace collapsar
