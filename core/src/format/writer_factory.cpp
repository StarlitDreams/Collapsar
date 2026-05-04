#include "i_writer.hpp"

#include "gzip_writer.hpp"
#include "zip_writer.hpp"

namespace collapsar {

Result<std::unique_ptr<IWriter>> make_writer(
    Format                       format,
    const std::filesystem::path& output,
    bool                          overwrite)
{
    switch (format) {
        case Format::Zip:
            return std::unique_ptr<IWriter>{new ZipWriter{output, overwrite}};
        case Format::Gzip:
            return std::unique_ptr<IWriter>{new GzipWriter{output, overwrite}};
        case Format::Zstd:
            return make_error(StatusCode::Unsupported,
                              "Zstd writer not yet implemented");
    }
    return make_error(StatusCode::InvalidArgument, "Unknown format");
}

} // namespace collapsar
