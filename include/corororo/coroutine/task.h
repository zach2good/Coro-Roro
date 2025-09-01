#pragma once

#include <corororo/coroutine/promise.h>
#include <corororo/coroutine/types.h>
#include <coroutine>
#include <utility>

// Forward declarations to avoid circular dependency
namespace CoroRoro {
    template <typename T> struct AsyncTaskPromise;
}

namespace CoroRoro
{

//
// Task
//
//   A coroutine task that executes immediately on the current thread.
//   Uses std::suspend_never for immediate execution.
//
//
// PromiseBase - Common base for all promise types
//
template <typename T>
struct PromiseBase
{
    PromiseBase() = default;
    virtual ~PromiseBase() = default;

    // Data storage for non-void types
    T data_{};

    // Thread affinity for this promise
    ThreadAffinity threadAffinity_ = ThreadAffinity::Main;

    // Common promise methods
    void unhandled_exception()
    {
        std::terminate();
    }
};

// Void specialization
template <>
struct PromiseBase<void>
{
    PromiseBase() = default;
    virtual ~PromiseBase() = default;

    // Thread affinity for this promise
    ThreadAffinity threadAffinity_ = ThreadAffinity::Main;

    // Common promise methods
    void unhandled_exception()
    {
        std::terminate();
    }
};

//
// TaskBase - Base struct for Task and AsyncTask
//
template <typename T, typename PromiseType, ThreadAffinity Affinity>
struct TaskBase
{
    using ResultType = T;
    using promise_type = PromiseType;
    static constexpr ThreadAffinity affinity = Affinity;

    TaskBase() noexcept = default;

    TaskBase(std::coroutine_handle<promise_type> coroutine) noexcept
        : handle_(coroutine)
    {
    }

    TaskBase(TaskBase const&) = delete;
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
            handle_ = other.handle_;
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

public:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

//
// Task - Main thread execution
//
template <typename T = void>
struct Task : TaskBase<T, detail::Promise<Task<T>, T>, ThreadAffinity::Main>
{
    using Base = TaskBase<T, detail::Promise<Task<T>, T>, ThreadAffinity::Main>;
    using promise_type = detail::Promise<Task<T>, T>;
    using Base::Base; // Inherit constructors
};

// Void specialization for Task
template <>
struct Task<void> : TaskBase<void, detail::Promise<Task<void>, void>, ThreadAffinity::Main>
{
    using Base = TaskBase<void, detail::Promise<Task<void>, void>, ThreadAffinity::Main>;
    using promise_type = detail::Promise<Task<void>, void>;
    using Base::Base; // Inherit constructors

    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }
};

//
// AsyncTask - Worker thread execution
//
template <typename T = void>
struct AsyncTask : TaskBase<T, AsyncTaskPromise<T>, ThreadAffinity::Worker>
{
    using Base = TaskBase<T, AsyncTaskPromise<T>, ThreadAffinity::Worker>;
    using promise_type = AsyncTaskPromise<T>;
    using Base::Base; // Inherit constructors
};

// Void specialization for AsyncTask
template <>
struct AsyncTask<void> : TaskBase<void, AsyncTaskPromise<void>, ThreadAffinity::Worker>
{
    using Base = TaskBase<void, AsyncTaskPromise<void>, ThreadAffinity::Worker>;
    using promise_type = AsyncTaskPromise<void>;
    using Base::Base; // Inherit constructors

    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }
};

} // namespace CoroRoro

// Include the actual definitions after forward declarations
#include <corororo/coroutine/async_task.h>
