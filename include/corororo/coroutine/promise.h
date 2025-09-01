#pragma once

#include <corororo/coroutine/types.h>
#include <corororo/scheduler/scheduler.h>
#include <coroutine>
#include <type_traits>

namespace CoroRoro {

// Forward declaration - the full Scheduler class is defined in scheduler.h
class Scheduler;

namespace detail {

// Specialized awaiters to avoid C4737 tail call optimization issues
template <ThreadAffinity Affinity>
struct InitialAwaiter;

// Main thread specialization
template <>
struct InitialAwaiter<ThreadAffinity::Main> {
    Scheduler* scheduler_;

    InitialAwaiter(Scheduler* sched) : scheduler_(sched) {}

    bool await_ready() const noexcept {
        return false; // Always suspend for scheduling
    }

    auto await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept {
        // Ultra-simple to avoid C4737 - just return noop for now
        // TODO: Re-implement scheduling logic without complex call chains
        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

// Worker thread specialization
template <>
struct InitialAwaiter<ThreadAffinity::Worker> {
    Scheduler* scheduler_;

    InitialAwaiter(Scheduler* sched) : scheduler_(sched) {}

    bool await_ready() const noexcept {
        return false; // Always suspend for scheduling
    }

    auto await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept {
        // Ultra-simple to avoid C4737 - just return noop for now
        // TODO: Re-implement scheduling logic without complex call chains
        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

template <ThreadAffinity Affinity>
struct FinalAwaiter;

// Main thread specialization
template <>
struct FinalAwaiter<ThreadAffinity::Main> {
    Scheduler* scheduler_;

    FinalAwaiter(Scheduler* sched) : scheduler_(sched) {}

    bool await_ready() const noexcept {
        return false; // Always suspend for final await
    }

    auto await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<> {
        // Ultra-simple to avoid C4737 - just return noop for now
        // TODO: Re-implement finalization logic without complex call chains
        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

// Worker thread specialization
template <>
struct FinalAwaiter<ThreadAffinity::Worker> {
    Scheduler* scheduler_;

    FinalAwaiter(Scheduler* sched) : scheduler_(sched) {}

    bool await_ready() const noexcept {
        return false; // Always suspend for final await
    }

    auto await_suspend([[maybe_unused]] std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<> {
        // Ultra-simple to avoid C4737 - just return noop for now
        // TODO: Re-implement finalization logic without complex call chains
        return std::noop_coroutine();
    }

    void await_resume() const noexcept {}
};

} // namespace detail

} // namespace CoroRoro