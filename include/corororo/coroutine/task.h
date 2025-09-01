#pragma once

#include <corororo/coroutine/promise.h>
#include <corororo/coroutine/types.h>
#include <coroutine>
#include <utility>

namespace CoroRoro
{

// Forward declarations
template <typename T>
struct AsyncTask;
template <typename T>
struct AsyncTaskPromise;

// Forward declaration - methods needed by awaiters
class Scheduler
{
public:
    std::thread::id getMainThreadId() const;
    void scheduleHandleWithAffinity(std::coroutine_handle<> handle, ThreadAffinity affinity);
    std::coroutine_handle<> getNextMainThreadTask();
    std::coroutine_handle<> getNextWorkerTask();
};

//
// TaskBase - Simplified base struct with baked-in affinity (no CRTP complexity)
//
template <ThreadAffinity Affinity, typename T>
struct TaskBase
{
    using ResultType                         = T;
    static constexpr ThreadAffinity affinity = Affinity;

    // Promise type depends on affinity
    using promise_type = std::conditional_t<Affinity == ThreadAffinity::Main,
                                           detail::TaskPromise<TaskBase<Affinity, T>, T>,
                                           AsyncTaskPromise<T>>;

    TaskBase() noexcept = default;

    TaskBase(std::coroutine_handle<promise_type> coroutine) noexcept
    : handle_(coroutine)
    {
    }

    TaskBase(TaskBase const&)            = delete;
    TaskBase& operator=(TaskBase const&) = delete;

    TaskBase(TaskBase&& other) noexcept
    : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    TaskBase& operator=(TaskBase&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
            {
                handle_.destroy();
            }
            handle_       = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~TaskBase() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    // Check if the task is done
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    // Get the result (only valid after the task is done) - non-void only
    template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
    auto result() -> U&
    {
        return handle_.promise().result();
    }

    // Get the result (const version) - non-void only
    template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
    auto result() const -> const U&
    {
        return handle_.promise().result();
    }

    // Awaitable interface
    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> std::coroutine_handle<>
    {
        return detail::await_suspend(handle_, coroutine);
    }

    // await_resume - non-void specialization
    template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
    auto await_resume() -> U&
    {
        return result();
    }

    // await_resume - void specialization
    template <typename U = T, typename = std::enable_if_t<std::is_void_v<U>>>
    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }

public:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

//
// Task - Simple alias for main thread execution
//
template <typename T = void>
using Task = TaskBase<ThreadAffinity::Main, T>;

// AsyncTask - Simple alias for worker thread execution
template <typename T>
using AsyncTask = TaskBase<ThreadAffinity::Worker, T>;

//
// Custom initial awaiter for AsyncTask types - uses compile-time affinity for symmetric transfer
struct AsyncTaskInitialAwaiter
{
    Scheduler* scheduler = nullptr;

    AsyncTaskInitialAwaiter(Scheduler* sched) : scheduler(sched) {}

    // Use compile-time affinity optimization - AsyncTask<T> always targets Worker thread
    bool await_ready() const noexcept
    {
        // Compile-time optimization: AsyncTask<T> has ThreadAffinity::Worker
        // Check if we're already on a worker thread without expensive lookups
        if constexpr (true) {  // We know this is for Worker affinity at compile time
            if (ThreadContext::current && ThreadContext::current->affinity == ThreadAffinity::Worker) {
                return true;  // Already on worker thread - proceed immediately!
            }
        }
        return false;  // Need to transfer to worker thread
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> coroutine) noexcept
    {
        // 🎯 Use TransferPolicy dispatcher for compile-time optimized transfer to Worker thread
        if (scheduler) {
            return dispatchTransfer<ThreadAffinity::Worker>(scheduler, coroutine);
        }

        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
        // Nothing to do when we resume
    }
};

// Custom final awaiter for AsyncTask types - enables symmetric transfer on completion
struct AsyncTaskFinalAwaiter
{
    Scheduler* scheduler = nullptr;

    AsyncTaskFinalAwaiter(Scheduler* sched) : scheduler(sched) {}

    bool await_ready() const noexcept
    {
        return false; // Always suspend to allow continuation
    }

    template <typename Promise>
    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> coroutine) noexcept
    {
        // Check if there's a continuation to resume
        auto& promise = coroutine.promise();
        if (promise.continuation_) {
            // We have a continuation - check if we can do symmetric transfer
            if (scheduler) {
                // Try to transfer to next task on the same thread affinity
                return scheduler->getNextWorkerTask();
            }
            return promise.continuation_;
        }

        return std::noop_coroutine();
    }

    void await_resume() const noexcept
    {
        // Nothing to do when we resume
    }
};

// CRTP Promise for AsyncTask types (defined after AsyncTask to use it)
//
template <typename T>
struct AsyncTaskPromise : detail::PromiseBase<AsyncTaskPromise<T>, T>
{
    AsyncTaskPromise()
    {
        // Set thread affinity for AsyncTasks (for compatibility)
        this->threadAffinity_ = ThreadAffinity::Worker;
    }

    auto get_return_object() noexcept -> AsyncTask<T>
    {
        return AsyncTask<T>{ std::coroutine_handle<AsyncTaskPromise<T>>::from_promise(*this) };
    }

    void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        if constexpr (!std::is_void_v<T>)
        {
            this->data_ = std::move(value);
        }
    }

    void return_value(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        if constexpr (!std::is_void_v<T>)
        {
            this->data_ = value;
        }
    }

    auto result() -> std::conditional_t<!std::is_void_v<T>, T&, void>
    {
        if constexpr (!std::is_void_v<T>)
        {
            return this->data_;
        }
    }

    auto initial_suspend() const noexcept -> AsyncTaskInitialAwaiter
    {
        return AsyncTaskInitialAwaiter{this->scheduler_};
    }

    auto final_suspend() noexcept -> AsyncTaskFinalAwaiter
    {
        return AsyncTaskFinalAwaiter{this->scheduler_};
    }
};

// Void specialization for CRTP AsyncTaskPromise
template <>
struct AsyncTaskPromise<void> : detail::PromiseBase<AsyncTaskPromise<void>, void>
{
    AsyncTaskPromise()
    {
        // Set thread affinity for AsyncTasks (for compatibility)
        this->threadAffinity_ = ThreadAffinity::Worker;
    }

    auto get_return_object() noexcept -> AsyncTask<void>
    {
        return AsyncTask<void>{ std::coroutine_handle<AsyncTaskPromise<void>>::from_promise(*this) };
    }

    void return_void() noexcept
    {
    }

    void result()
    {
        // void tasks don't have a result
    }

    auto initial_suspend() const noexcept -> AsyncTaskInitialAwaiter
    {
        return AsyncTaskInitialAwaiter{this->scheduler_};
    }

    auto final_suspend() noexcept -> AsyncTaskFinalAwaiter
    {
        return AsyncTaskFinalAwaiter{this->scheduler_};
    }
};

} // namespace CoroRoro
