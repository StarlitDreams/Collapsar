#include "log.hpp"

#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

namespace collapsar::log_detail {
namespace {

std::atomic<Level>      g_level{Level::Info};
std::mutex              g_mutex;

const char* tag(Level level) noexcept {
    switch (level) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?    ";
}

} // namespace

void  set_level(Level level) noexcept { g_level.store(level, std::memory_order_relaxed); }
Level current_level()        noexcept { return g_level.load(std::memory_order_relaxed); }

LineGuard::LineGuard(Level level)
    : active_{level >= current_level()},
      lock_{g_mutex, std::defer_lock} {
    if (!active_) return;
    lock_.lock();
    using clock = std::chrono::system_clock;
    const auto now    = clock::now();
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    const auto t      = clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    auto& os = stream();
    os << '[' << std::put_time(&tm, "%H:%M:%S") << '.'
       << std::setw(3) << std::setfill('0') << millis << "] "
       << tag(level) << ' ';
}

LineGuard::~LineGuard() {
    if (active_) {
        stream() << '\n';
    }
}

std::ostream& LineGuard::stream() { return std::cerr; }

} // namespace collapsar::log_detail
