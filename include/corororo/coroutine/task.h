#pragma once

#include <corororo/coroutine/promise.h>
#include <corororo/coroutine/types.h>
#include <coroutine>
#include <utility>

namespace CoroRoro
{

// Forward declaration
class Scheduler;

// Forward declaration for TaskBase
template <ThreadAffinity Affinity, typename T>
struct TaskBase;

// Awaiters are defined in promise.h - using existing TaskInitialAwaiter and FinalAwaiter

namespace detail {

//
// Generic Promise - Non-void specialization
//
template <ThreadAffinity Affinity, typename T>
struct Promise {
    static constexpr ThreadAffinity affinity = Affinity;

    Scheduler* scheduler_ = nullptr;
    std::coroutine_handle<> continuation_ = nullptr;
    T data_;

    // Simplified to avoid C4737
    auto get_return_object() noexcept {
        return TaskBase<Affinity, T>{
            std::coroutine_handle<Promise<Affinity, T>>::from_promise(*this)
        };
    }

    // Use custom awaiters for performance - but keep them simple to avoid C4737
    auto initial_suspend() const noexcept {
        return detail::InitialAwaiter<Affinity>{scheduler_};
    }

    auto final_suspend() noexcept {
        return detail::FinalAwaiter<Affinity>{scheduler_};
    }

    // Only return_value for non-void types
    void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>) {
        data_ = std::move(value);
    }

    void return_value(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        data_ = value;
    }

    auto result() -> T& {
        return data_;
    }

    void unhandled_exception() noexcept { std::terminate(); }

    // Await transform - simplified to avoid C4737
    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept {
        return std::forward<AwaitableType>(awaitable);
    }

    template <typename U>
    void yield_value(U&&) = delete;
};

// Specialization for void types
template <ThreadAffinity Affinity>
struct Promise<Affinity, void> {
    static constexpr ThreadAffinity affinity = Affinity;

    Scheduler* scheduler_ = nullptr;
    std::coroutine_handle<> continuation_ = nullptr;

    // Simplified to avoid C4737
    auto get_return_object() noexcept {
        return TaskBase<Affinity, void>{
            std::coroutine_handle<Promise<Affinity, void>>::from_promise(*this)
        };
    }

    // Use custom awaiters for performance - but keep them simple to avoid C4737
    auto initial_suspend() const noexcept {
        return detail::InitialAwaiter<Affinity>{scheduler_};
    }

    auto final_suspend() noexcept {
        return detail::FinalAwaiter<Affinity>{scheduler_};
    }

    // Only return_void for void types
    void return_void() noexcept {}

    void unhandled_exception() noexcept { std::terminate(); }

    // Await transform - simplified to avoid C4737
    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept {
        return std::forward<AwaitableType>(awaitable);
    }

    template <typename U>
    void yield_value(U&&) = delete;
};

} // namespace detail

//
// TaskBase - Simplified base struct with baked-in affinity (no CRTP complexity)
//
template <ThreadAffinity Affinity, typename T>
struct TaskBase
{
    using ResultType                         = T;
    static constexpr ThreadAffinity affinity = Affinity;

    // Generic promise type - affinity is handled through templates
    using promise_type = detail::Promise<Affinity, T>;

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
        // For now, just resume the awaiting coroutine directly
        return coroutine;
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

// Specialization for void types - no data member needed
template <ThreadAffinity Affinity>
struct TaskBase<Affinity, void>
{
    using ResultType                         = void;
    static constexpr ThreadAffinity affinity = Affinity;

    // Generic promise type - affinity is handled through templates
    using promise_type = detail::Promise<Affinity, void>;

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

    // No result method for void types - they don't return values

    // Awaitable interface
    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> std::coroutine_handle<>
    {
        // For now, just resume the awaiting coroutine directly
        return coroutine;
    }

    // await_resume for void - nothing to return
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



} // namespace CoroRoro
