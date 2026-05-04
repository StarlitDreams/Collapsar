#pragma once

#include <thread>
#include <utility>

namespace collapsar {

// RAII wrapper that joins on destruction. Saves us from sprinkling try/catch
// around every worker-spawn site.
class ScopedThread {
public:
    ScopedThread() noexcept = default;

    template <typename Fn, typename... Args>
    explicit ScopedThread(Fn&& fn, Args&&... args)
        : thread_{std::forward<Fn>(fn), std::forward<Args>(args)...} {}

    ScopedThread(ScopedThread&&) noexcept = default;
    ScopedThread& operator=(ScopedThread&& other) noexcept {
        if (this != &other) {
            join_if_joinable();
            thread_ = std::move(other.thread_);
        }
        return *this;
    }

    ScopedThread(const ScopedThread&) = delete;
    ScopedThread& operator=(const ScopedThread&) = delete;

    ~ScopedThread() { join_if_joinable(); }

    [[nodiscard]] bool joinable() const noexcept { return thread_.joinable(); }
    void join() { if (thread_.joinable()) thread_.join(); }

private:
    void join_if_joinable() noexcept {
        if (thread_.joinable()) {
            try { thread_.join(); } catch (...) { /* swallow during dtor */ }
        }
    }

    std::thread thread_;
};

} // namespace collapsar
