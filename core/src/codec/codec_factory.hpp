#pragma once

#include <memory>

#include "collapsar/options.hpp"
#include "collapsar/result.hpp"
#include "i_codec.hpp"

namespace collapsar {

struct CodecBundle {
    std::unique_ptr<ICodec>      cpu;     // always present
    std::unique_ptr<IBatchCodec> gpu;     // present iff CUDA + chosen format supports GPU
};

// Builds the codec pair the pipeline will use for a job. The choice depends
// on (a) format (ZIP requires standard deflate) and (b) whether GPU is enabled
// and available.
[[nodiscard]] Result<CodecBundle> make_codecs(const EngineOptions& engine,
                                               const JobOptions&    job);

} // namespace collapsar
