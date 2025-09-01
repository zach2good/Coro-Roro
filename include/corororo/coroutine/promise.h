#pragma once

#include <corororo/coroutine/types.h>
#include <corororo/scheduler/scheduler.h>
#include <coroutine>
#include <type_traits>

namespace CoroRoro {

// Forward declaration - the full Scheduler class is defined in scheduler.h
class Scheduler;

namespace detail {

// Generic awaiters with affinity baked in as template parameters
template <ThreadAffinity Affinity>
struct InitialAwaiter {
    Scheduler* scheduler_;

    InitialAwaiter(Scheduler* sched) : scheduler_(sched) {}

    bool await_ready() const noexcept {
        return false; // Always suspend for scheduling
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept {
        // Very simple call to avoid tail call optimization issues
        scheduler_->scheduleMainThreadTask(handle);
        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

template <ThreadAffinity Affinity>
struct FinalAwaiter {
    Scheduler* scheduler_;

    FinalAwaiter(Scheduler* sched) : scheduler_(sched) {}

    bool await_ready() const noexcept {
        return false; // Always suspend for final await
    }

    auto await_suspend(std::coroutine_handle<> handle) noexcept {
        // Very simple call to avoid tail call optimization issues
        return scheduler_->finalizeMainThreadTask(handle);
    }

    void await_resume() const noexcept {}
};

} // namespace detail

} // namespace CoroRoro