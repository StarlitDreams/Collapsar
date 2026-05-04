#pragma once

#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace collapsar {

// Status codes returned across the public boundary.
//
// Kept as a closed enum so that the C++/CLI bridge can map each value to a
// corresponding managed enum without losing type safety. Add new values at the
// end so the wire-form values stay stable.
enum class StatusCode {
    Ok = 0,
    Cancelled,
    InvalidArgument,
    NotFound,
    PermissionDenied,
    Io,
    OutOfMemory,
    CudaError,
    NvcompError,
    CodecError,
    FormatError,
    Unsupported,
    Internal,
};

[[nodiscard]] constexpr std::string_view to_string(StatusCode code) noexcept {
    switch (code) {
        case StatusCode::Ok:               return "Ok";
        case StatusCode::Cancelled:        return "Cancelled";
        case StatusCode::InvalidArgument:  return "InvalidArgument";
        case StatusCode::NotFound:         return "NotFound";
        case StatusCode::PermissionDenied: return "PermissionDenied";
        case StatusCode::Io:               return "Io";
        case StatusCode::OutOfMemory:      return "OutOfMemory";
        case StatusCode::CudaError:        return "CudaError";
        case StatusCode::NvcompError:      return "NvcompError";
        case StatusCode::CodecError:       return "CodecError";
        case StatusCode::FormatError:      return "FormatError";
        case StatusCode::Unsupported:      return "Unsupported";
        case StatusCode::Internal:         return "Internal";
    }
    return "Unknown";
}

class Error {
public:
    Error() = default;
    Error(StatusCode code, std::string message)
        : code_{code}, message_{std::move(message)} {}

    [[nodiscard]] StatusCode         code()    const noexcept { return code_;    }
    [[nodiscard]] const std::string& message() const noexcept { return message_; }
    [[nodiscard]] bool               ok()      const noexcept { return code_ == StatusCode::Ok; }

    explicit operator bool() const noexcept { return ok(); }

    friend std::ostream& operator<<(std::ostream& os, const Error& e) {
        return os << to_string(e.code_) << ": " << e.message_;
    }

private:
    StatusCode  code_{StatusCode::Ok};
    std::string message_;
};

[[nodiscard]] inline Error make_error(StatusCode code, std::string message) {
    return Error{code, std::move(message)};
}

} // namespace collapsar
