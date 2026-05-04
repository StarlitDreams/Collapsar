#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace collapsar {

// Multi-producer, multi-consumer bounded queue with explicit closing.
//
// Producers block on push() when the queue is full; consumers block on pop()
// when it's empty. Once close() has been called, push() returns false
// immediately and pop() drains any remaining items before returning std::nullopt.
//
// This is good enough for the pipeline stages (4-8 producers, 4-8 consumers,
// 64-256 elements). For higher contention, swap in a lock-free MPMC ring.

template <typename T>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity_{capacity} {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    bool push(T value) {
        std::unique_lock lock{mutex_};
        not_full_.wait(lock, [&] { return closed_ || items_.size() < capacity_; });
        if (closed_) return false;
        items_.emplace(std::move(value));
        lock.unlock();
        not_empty_.notify_one();
        return true;
    }

    [[nodiscard]] std::optional<T> pop() {
        std::unique_lock lock{mutex_};
        not_empty_.wait(lock, [&] { return closed_ || !items_.empty(); });
        if (items_.empty()) return std::nullopt;  // closed and drained
        T value = std::move(items_.front());
        items_.pop();
        lock.unlock();
        not_full_.notify_one();
        return value;
    }

    // Non-blocking pop. Returns nullopt if the queue is empty (regardless of
    // close state).
    [[nodiscard]] std::optional<T> try_pop() {
        std::unique_lock lock{mutex_};
        if (items_.empty()) return std::nullopt;
        T value = std::move(items_.front());
        items_.pop();
        lock.unlock();
        not_full_.notify_one();
        return value;
    }

    void close() {
        {
            std::scoped_lock lock{mutex_};
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    [[nodiscard]] bool        closed() const noexcept { return closed_; }
    [[nodiscard]] std::size_t size()   const {
        std::scoped_lock lock{mutex_};
        return items_.size();
    }

private:
    mutable std::mutex      mutex_;
    std::condition_variable not_full_;
    std::condition_variable not_empty_;
    std::queue<T>           items_;
    std::size_t             capacity_;
    bool                    closed_ = false;
};

} // namespace collapsar
