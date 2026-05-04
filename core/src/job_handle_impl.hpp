#pragma once

#include <future>
#include <mutex>
#include <stop_token>

#include "collapsar/job.hpp"
#include "collapsar/result.hpp"

namespace collapsar {

// Backing state for JobHandle. Held by shared_ptr — both the JobHandle the
// caller holds and the worker thread driving the pipeline keep references.
//
// Two mutexes by design:
//   * result_mutex_ guards the future and the cached result. wait() releases
//     it before blocking on the future so other threads can poll progress.
//   * progress_mutex_ guards the snapshot the pipeline publishes from its
//     own thread. Keeping this disjoint from result_mutex_ avoids a deadlock
//     where wait() holds the result mutex while the producer wants the
//     progress one (or vice versa).
class JobHandleImpl {
public:
    JobHandleImpl();

    [[nodiscard]] std::stop_token get_stop_token() const noexcept { return stop_.get_token(); }

    void          request_stop()   noexcept { stop_.request_stop(); }
    bool          stop_requested() const noexcept { return stop_.stop_requested(); }

    void          set_future(std::future<Result<JobStats>> future);
    void          publish_progress(const ProgressEvent& event);

    void          wait();
    [[nodiscard]] bool          done()     const noexcept;
    [[nodiscard]] Error         error()    const;
    [[nodiscard]] JobStats      stats()    const;
    [[nodiscard]] ProgressEvent progress() const;

private:
    void resolve_locked() const;  // requires result_mutex_ held

    std::stop_source                                  stop_;

    mutable std::mutex                                result_mutex_;
    mutable std::future<Result<JobStats>>             future_;
    mutable bool                                      resolved_ = false;
    mutable JobStats                                  stats_{};
    mutable Error                                     error_{};

    mutable std::mutex                                progress_mutex_;
    ProgressEvent                                     progress_{};
};

} // namespace collapsar
