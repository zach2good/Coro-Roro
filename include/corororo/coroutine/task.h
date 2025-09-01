#pragma once

#include <corororo/coroutine/promise.h>
#include <corororo/coroutine/types.h>
#include <coroutine>
#include <utility>

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
class PromiseBase
{
public:
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
class PromiseBase<void>
{
public:
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
// TaskBase - CRTP base class for Task and AsyncTask
//
template <typename Derived, typename T, ThreadAffinity Affinity>
class TaskBase
{
public:
    using ResultType = T;
    static constexpr ThreadAffinity affinity = Affinity;

    TaskBase() noexcept = default;

    TaskBase(std::coroutine_handle<typename Derived::promise_type> coroutine) noexcept
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

    // Get the underlying coroutine handle for scheduler access
    auto getHandle() const noexcept -> std::coroutine_handle<typename Derived::promise_type>
    {
        return handle_;
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

private:
    std::coroutine_handle<typename Derived::promise_type> handle_ = nullptr;
};

//
// Task - Main thread execution
//
template <typename T = void>
class Task : public TaskBase<Task<T>, T, ThreadAffinity::Main>
{
public:
    using Base = TaskBase<Task<T>, T, ThreadAffinity::Main>;
    using promise_type = detail::Promise<Task<T>, T>;
    using Base::Base; // Inherit constructors
};

// Void specialization for Task
template <>
class Task<void> : public TaskBase<Task<void>, void, ThreadAffinity::Main>
{
public:
    using Base = TaskBase<Task<void>, void, ThreadAffinity::Main>;
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
class AsyncTask : public TaskBase<AsyncTask<T>, T, ThreadAffinity::Worker>
{
public:
    using Base = TaskBase<AsyncTask<T>, T, ThreadAffinity::Worker>;
    using promise_type = detail::Promise<AsyncTask<T>, T>;
    using Base::Base; // Inherit constructors
};

// Void specialization for AsyncTask
template <>
class AsyncTask<void> : public TaskBase<AsyncTask<void>, void, ThreadAffinity::Worker>
{
public:
    using Base = TaskBase<AsyncTask<void>, void, ThreadAffinity::Worker>;
    using promise_type = detail::Promise<AsyncTask<void>, void>;
    using Base::Base; // Inherit constructors

    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }
};

} // namespace CoroRoro
