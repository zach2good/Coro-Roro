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

//
// CRTP TaskBase - Base struct for Task and AsyncTask (no virtual functions)
//
template <typename Derived, typename T, typename PromiseType, ThreadAffinity Affinity>
struct TaskBase
{
    using ResultType                         = T;
    using promise_type                       = PromiseType;
    static constexpr ThreadAffinity affinity = Affinity;

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

    // CRTP helper - get derived instance
    auto derived() -> Derived&
    {
        return static_cast<Derived&>(*this);
    }

    auto derived() const -> const Derived&
    {
        return static_cast<const Derived&>(*this);
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

    // await_resume - non-void specialization (can be overridden by derived classes)
    template <typename U = T, typename = std::enable_if_t<!std::is_void_v<U>>>
    auto await_resume() -> U&
    {
        return result();
    }

public:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

//
// Task - Main thread execution (CRTP-based)
//
template <typename T = void>
struct Task : TaskBase<Task<T>, T, detail::TaskPromise<Task<T>, T>, ThreadAffinity::Main>
{
    using Base         = TaskBase<Task<T>, T, detail::TaskPromise<Task<T>, T>, ThreadAffinity::Main>;
    using promise_type = detail::TaskPromise<Task<T>, T>;
    using Base::Base; // Inherit constructors
};

// Void specialization for Task (CRTP-based)
template <>
struct Task<void> : TaskBase<Task<void>, void, detail::TaskPromise<Task<void>, void>, ThreadAffinity::Main>
{
    using Base         = TaskBase<Task<void>, void, detail::TaskPromise<Task<void>, void>, ThreadAffinity::Main>;
    using promise_type = detail::TaskPromise<Task<void>, void>;
    using Base::Base; // Inherit constructors

    // Override await_resume for void specialization
    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }
};

//
// AsyncTask - Worker thread execution (CRTP-based)
//
template <typename T>
struct AsyncTask : TaskBase<AsyncTask<T>, T, AsyncTaskPromise<T>, ThreadAffinity::Worker>
{
    using Base         = TaskBase<AsyncTask<T>, T, AsyncTaskPromise<T>, ThreadAffinity::Worker>;
    using promise_type = AsyncTaskPromise<T>;
    using Base::Base; // Inherit constructors
};

// Void specialization for AsyncTask (CRTP-based)
template <>
struct AsyncTask<void> : TaskBase<AsyncTask<void>, void, AsyncTaskPromise<void>, ThreadAffinity::Worker>
{
    using Base         = TaskBase<AsyncTask<void>, void, AsyncTaskPromise<void>, ThreadAffinity::Worker>;
    using promise_type = AsyncTaskPromise<void>;
    using Base::Base; // Inherit constructors

    // Override await_resume for void specialization
    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }
};

//
// CRTP Promise for AsyncTask types (defined after AsyncTask to use it)
//
template <typename T>
struct AsyncTaskPromise : detail::PromiseBase<AsyncTaskPromise<T>, T>
{
    AsyncTaskPromise()
    {
        this->threadAffinity_ = ThreadAffinity::Worker; // AsyncTasks run on worker threads
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

    auto initial_suspend() const noexcept -> std::suspend_always
    {
        return {};
    }

    auto final_suspend() noexcept -> detail::FinalAwaiter<AsyncTaskPromise<T>>
    {
        return {};
    }
};

// Void specialization for CRTP AsyncTaskPromise
template <>
struct AsyncTaskPromise<void> : detail::PromiseBase<AsyncTaskPromise<void>, void>
{
    AsyncTaskPromise()
    {
        this->threadAffinity_ = ThreadAffinity::Worker; // AsyncTasks run on worker threads
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

    auto initial_suspend() const noexcept -> std::suspend_always
    {
        return {};
    }

    auto final_suspend() noexcept -> detail::FinalAwaiter<AsyncTaskPromise<void>>
    {
        return {};
    }
};

} // namespace CoroRoro
