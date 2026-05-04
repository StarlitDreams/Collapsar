#include "thread_pool.hpp"

namespace collapsar {

ThreadPool::ThreadPool(std::size_t worker_count) {
    if (worker_count == 0) worker_count = 1;
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    stop();
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
}

void ThreadPool::stop() {
    {
        std::scoped_lock lock{mutex_};
        stopped_ = true;
    }
    cv_.notify_all();
}

void ThreadPool::worker_loop() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock{mutex_};
            cv_.wait(lock, [&] { return stopped_ || !tasks_.empty(); });
            if (tasks_.empty()) return;  // stopped and drained
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try { task(); } catch (...) { /* worker swallows; futures see exception */ }
    }
}

} // namespace collapsar
