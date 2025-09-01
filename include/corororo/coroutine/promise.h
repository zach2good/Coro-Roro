#pragma once

#include <corororo/coroutine/types.h>
#include <corororo/scheduler/scheduler.h>
#include <coroutine>
#include <type_traits>

namespace CoroRoro {

// Forward declaration - the full Scheduler class is defined in scheduler.h
class Scheduler;

namespace detail {

// Final awaiter that handles coroutine completion with scheduler delegation
template <typename DerivedPromise>
struct FinalAwaiter final
{
    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<DerivedPromise> h) noexcept
    {
        auto& promise = h.promise();

        // Use scheduler's direct methods for tail call optimization
        if (promise.scheduler_)
        {
            if constexpr (DerivedPromise::affinity == ThreadAffinity::Main) {
                return promise.scheduler_->finalizeMainThreadTaskDirect(h);
            } else {
                return promise.scheduler_->finalizeWorkerThreadTaskDirect(h);
            }
        }

        // Fallback to continuation if no scheduler
        if (promise.continuation_)
        {
            return promise.continuation_;
        }

        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }
};

// Custom initial awaiter for Task types - uses compile-time affinity for symmetric transfer
struct TaskInitialAwaiter
{
    Scheduler* scheduler;

    TaskInitialAwaiter(Scheduler* sched) : scheduler(sched) {}

    // Use compile-time affinity optimization - Task<T> always targets Main thread
    bool await_ready() const noexcept
    {
        // For generic awaiters, we always suspend to allow scheduling
        return false;
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> coroutine) noexcept
    {
        // Use scheduler's direct method for tail call optimization
        if (scheduler) {
            return scheduler->scheduleMainThreadTaskDirect(coroutine);
        }

        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
    }
};

// Custom final awaiter for Task types - enables symmetric transfer on completion
struct TaskFinalAwaiter
{
    Scheduler* scheduler;

    TaskFinalAwaiter(Scheduler* sched) : scheduler(sched) {}

    bool await_ready() const noexcept
    {
        return false; // Always suspend to allow continuation
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> coroutine) noexcept
    {
        // Use scheduler's direct methods for tail call optimization
        if (scheduler) {
            if constexpr (Promise::affinity == ThreadAffinity::Main) {
                return scheduler->finalizeMainThreadTaskDirect(coroutine);
            } else {
                return scheduler->finalizeWorkerThreadTaskDirect(coroutine);
            }
        }

        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
        // Nothing to do when we resume
    }
};

} // namespace detail

} // namespace CoroRoro