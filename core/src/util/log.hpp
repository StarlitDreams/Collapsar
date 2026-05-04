#pragma once

#include <mutex>
#include <ostream>
#include <string>
#include <string_view>

// A trivially small logger. We don't want fmt or spdlog as a hard dependency
// for the core library, so we wrap iostreams behind a level-checked macro.
//
// Replace with whatever your project standardizes on; the call sites use
// COLLAPSAR_LOG_*, so swapping the backend is a one-file change.

namespace collapsar::log_detail {

enum class Level { Trace, Debug, Info, Warn, Error };

void set_level(Level level) noexcept;
[[nodiscard]] Level current_level() noexcept;

// Acquires a process-global mutex and returns a pointer to the chosen sink.
// The returned stream must not be retained beyond the calling expression.
class LineGuard {
public:
    explicit LineGuard(Level level);
    ~LineGuard();

    template <typename T>
    LineGuard& operator<<(const T& value) {
        if (active_) { stream() << value; }
        return *this;
    }

    [[nodiscard]] bool active() const noexcept { return active_; }

private:
    static std::ostream& stream();

    bool active_;
    std::unique_lock<std::mutex> lock_;
};

} // namespace collapsar::log_detail

#define COLLAPSAR_LOG(level)                                                    \
    if (auto _line = ::collapsar::log_detail::LineGuard(::collapsar::log_detail::Level::level); _line.active()) \
        _line

#define COLLAPSAR_LOG_TRACE() COLLAPSAR_LOG(Trace)
#define COLLAPSAR_LOG_DEBUG() COLLAPSAR_LOG(Debug)
#define COLLAPSAR_LOG_INFO()  COLLAPSAR_LOG(Info)
#define COLLAPSAR_LOG_WARN()  COLLAPSAR_LOG(Warn)
#define COLLAPSAR_LOG_ERROR() COLLAPSAR_LOG(Error)
