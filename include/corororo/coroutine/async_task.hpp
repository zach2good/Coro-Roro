#pragma once

#include <corororo/coroutine/task.hpp>
#include <corororo/coroutine/types.hpp>
#include <coroutine>

namespace CoroRoro
{

// Forward declarations
template <typename T>
class AsyncTask;

template <typename T>
struct AsyncTaskPromise;

//
// AsyncTaskPromise for non-void types
//
template <typename T>
struct AsyncTaskPromise final : public detail::PromiseBase
{
    T data_;

    auto get_return_object() noexcept -> AsyncTask<T>
    {
        return {std::coroutine_handle<AsyncTaskPromise>::from_promise(*this)};
    }

    void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        data_ = std::move(value);
    }

    void return_value(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        data_ = value;
    }

    void unhandled_exception()
    {
        std::terminate();
    }

    auto result() -> T&
    {
        return data_;
    }

    // Automatically suspend to be scheduled on a worker thread
    auto initial_suspend() const noexcept -> std::suspend_always
    {
        return {};
    }

    auto final_suspend() noexcept -> detail::FinalAwaiter<AsyncTaskPromise>
    {
        return {};
    }
};

// Forward declare AsyncTask<void> specialization
template <>
class AsyncTask<void>;

// Forward declare AsyncTaskPromise<void> specialization
template <>
struct AsyncTaskPromise<void>;

//
// AsyncTask
//
//   A coroutine task that automatically suspends to be scheduled on worker threads.
//   Uses std::suspend_always for automatic suspension.
//
template <typename T = void>
class AsyncTask final
{
public:
    using promise_type = AsyncTaskPromise<T>;
    using ResultType = T;

    AsyncTask() noexcept = default;

    AsyncTask(std::coroutine_handle<promise_type> coroutine) noexcept
        : handle_{coroutine}
    {
    }

    AsyncTask(AsyncTask const&) = delete;
    AsyncTask& operator=(AsyncTask const&) = delete;

    AsyncTask(AsyncTask&& other) noexcept
        : handle_{other.handle_}
    {
        other.handle_ = nullptr;
    }

    AsyncTask& operator=(AsyncTask&& other) noexcept
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

    ~AsyncTask() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    // Get the underlying coroutine handle for scheduler access
    auto getHandle() const noexcept -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

    // Check if the task is done
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    // Get the result (only valid after the task is done)
    template <typename U = T, typename = std::enable_if_t<!std::is_same_v<U, void>>>
    auto result() -> U&
    {
        return handle_.promise().result();
    }

    // Get the result (const version)
    template <typename U = T, typename = std::enable_if_t<!std::is_same_v<U, void>>>
    auto result() const -> const U&
    {
        return handle_.promise().result();
    }

    // Legacy method for backward compatibility
    auto get_result() -> T&
    {
        return result();
    }

    // Legacy method for backward compatibility
    auto get_result() const -> const T&
    {
        return result();
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

    // Conditional await_resume for non-void types
    template <typename U = T, typename = std::enable_if_t<!std::is_same_v<U, void>>>
    auto await_resume() -> U&
    {
        return result();
    }

    // await_resume for void type
    template <typename U = T, typename = std::enable_if_t<std::is_same_v<U, void>>>
    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }

private:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

// Specialization for void AsyncTask
template <>
class AsyncTask<void> final
{
public:
    using promise_type = AsyncTaskPromise<void>;
    using ResultType = void;

    AsyncTask() noexcept = default;

    AsyncTask(std::coroutine_handle<promise_type> coroutine) noexcept
        : handle_{coroutine}
    {
    }

    AsyncTask(AsyncTask const&) = delete;
    AsyncTask& operator=(AsyncTask const&) = delete;

    AsyncTask(AsyncTask&& other) noexcept
        : handle_{other.handle_}
    {
        other.handle_ = nullptr;
    }

    AsyncTask& operator=(AsyncTask&& other) noexcept
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

    ~AsyncTask() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    // Get the underlying coroutine handle for scheduler access
    auto getHandle() const noexcept -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

    // Check if the task is done
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
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

    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }

private:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

// Specialization for void AsyncTaskPromise
template <>
struct AsyncTaskPromise<void> final : public detail::PromiseBase
{
    auto get_return_object() noexcept -> AsyncTask<void>
    {
        return {std::coroutine_handle<AsyncTaskPromise>::from_promise(*this)};
    }

    void return_void() noexcept
    {
    }

    void unhandled_exception()
    {
        std::terminate();
    }

    // Automatically suspend to be scheduled on a worker thread
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
