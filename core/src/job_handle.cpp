#include "collapsar/job.hpp"

#include <chrono>
#include <utility>

#include "job_handle_impl.hpp"

namespace collapsar {

JobHandleImpl::JobHandleImpl() = default;

void JobHandleImpl::set_future(std::future<Result<JobStats>> future) {
    std::scoped_lock lock{result_mutex_};
    future_ = std::move(future);
}

void JobHandleImpl::publish_progress(const ProgressEvent& event) {
    std::scoped_lock lock{progress_mutex_};
    progress_ = event;
}

// Caller holds result_mutex_. We can't block on future_.get() while holding
// the lock — wait() takes care of that — but if the result is already ready
// we can resolve in place.
void JobHandleImpl::resolve_locked() const {
    if (resolved_) return;
    if (!future_.valid()) {
        error_    = make_error(StatusCode::Internal, "JobHandle has no future");
        resolved_ = true;
        return;
    }
    if (future_.wait_for(std::chrono::seconds{0}) != std::future_status::ready) {
        return;  // not yet — caller can drop the lock and wait properly
    }
    auto result = future_.get();
    if (result) stats_ = std::move(result).value();
    else        error_ = std::move(result).error();
    resolved_ = true;
}

void JobHandleImpl::wait() {
    // Take the future under the lock, then block on it without the lock so the
    // producer thread can publish progress (which takes a different mutex).
    std::future<Result<JobStats>> local;
    {
        std::scoped_lock lock{result_mutex_};
        if (resolved_) return;
        if (!future_.valid()) {
            error_    = make_error(StatusCode::Internal, "JobHandle has no future");
            resolved_ = true;
            return;
        }
        local = std::move(future_);
    }

    auto result = local.get();

    std::scoped_lock lock{result_mutex_};
    if (result) stats_ = std::move(result).value();
    else        error_ = std::move(result).error();
    resolved_ = true;
}

bool JobHandleImpl::done() const noexcept {
    std::scoped_lock lock{result_mutex_};
    if (resolved_) return true;
    if (!future_.valid()) return false;
    return future_.wait_for(std::chrono::seconds{0}) == std::future_status::ready;
}

Error JobHandleImpl::error() const {
    {
        std::scoped_lock lock{result_mutex_};
        if (resolved_) return error_;
    }
    const_cast<JobHandleImpl*>(this)->wait();
    std::scoped_lock lock{result_mutex_};
    return error_;
}

JobStats JobHandleImpl::stats() const {
    {
        std::scoped_lock lock{result_mutex_};
        if (resolved_) return stats_;
    }
    const_cast<JobHandleImpl*>(this)->wait();
    std::scoped_lock lock{result_mutex_};
    return stats_;
}

ProgressEvent JobHandleImpl::progress() const {
    std::scoped_lock lock{progress_mutex_};
    return progress_;
}

// ---- JobHandle public surface (defined here to keep the impl coupled) -------

JobHandle::JobHandle(std::shared_ptr<JobHandleImpl> impl) : impl_{std::move(impl)} {}
JobHandle::~JobHandle() {
    if (impl_) impl_->request_stop();
}
JobHandle::JobHandle(JobHandle&&) noexcept            = default;
JobHandle& JobHandle::operator=(JobHandle&&) noexcept = default;

void          JobHandle::wait()                    { impl_->wait(); }
void          JobHandle::cancel()                  { impl_->request_stop(); }
bool          JobHandle::done()  const noexcept    { return impl_->done(); }
Error         JobHandle::error() const             { return impl_->error(); }
JobStats      JobHandle::stats() const             { return impl_->stats(); }
ProgressEvent JobHandle::progress() const          { return impl_->progress(); }

} // namespace collapsar
