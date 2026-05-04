#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace collapsar {

// Plain FIFO thread pool. Submitted tasks return a std::future for the result.
// Used for I/O reads and CPU-side compression. Not optimised for short tasks.
class ThreadPool {
public:
    explicit ThreadPool(std::size_t worker_count);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename Fn, typename... Args>
    auto submit(Fn&& fn, Args&&... args)
        -> std::future<std::invoke_result_t<Fn, Args...>>
    {
        using R = std::invoke_result_t<Fn, Args...>;
        auto task = std::make_shared<std::packaged_task<R()>>(
            [f = std::forward<Fn>(fn),
             tup = std::tuple{std::forward<Args>(args)...}]() mutable -> R {
                return std::apply(std::move(f), std::move(tup));
            });
        auto future = task->get_future();
        {
            std::scoped_lock lock{mutex_};
            if (stopped_) {
                throw std::runtime_error{"ThreadPool::submit on stopped pool"};
            }
            tasks_.emplace([task] { (*task)(); });
        }
        cv_.notify_one();
        return future;
    }

    // Shutdown is implicit in the destructor; this is here for early shutdown
    // (e.g. when cancellation is observed and we want to stop accepting work).
    void stop();

    [[nodiscard]] std::size_t size() const noexcept { return workers_.size(); }

private:
    void worker_loop();

    std::vector<std::thread>          workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex                        mutex_;
    std::condition_variable           cv_;
    bool                              stopped_ = false;
};

} // namespace collapsar
