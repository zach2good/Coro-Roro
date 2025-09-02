#pragma once

#include <corororo/coroutine/promise.h>
#include <corororo/coroutine/types.h>
#include <coroutine>
#include <exception>
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

// CRTP Base class for Promises to reduce code duplication and avoid C4737
template <typename Derived, ThreadAffinity Affinity, typename T>
struct PromiseBase
{
    Scheduler* scheduler_ = nullptr;
    std::coroutine_handle<> continuation_handle_ = nullptr;
    detail::ContinuationFnPtr continuation_fn_ = nullptr;

    CORO_HOT CORO_INLINE auto get_return_object() noexcept
    {
        return TaskBase<Affinity, T>{
            std::coroutine_handle<Derived>::from_promise(
                *static_cast<Derived*>(this))
        };
    }

    CORO_HOT CORO_INLINE auto initial_suspend() const noexcept
    {
        return InitialAwaiter{};
    }

    CORO_HOT CORO_INLINE auto final_suspend() noexcept
    {
        struct FinalTransitionAwaiter
        {
            PromiseBase* promise_;

            CORO_HOT CORO_INLINE bool await_ready() const noexcept
            {
                return false;
            }

            CORO_HOT std::coroutine_handle<> await_suspend(std::coroutine_handle<>) noexcept
            {
                if (promise_->continuation_fn_)
                {
                    return promise_->continuation_fn_(promise_->scheduler_, promise_->continuation_handle_);
                }
                return promise_->scheduler_->template getNextTaskWithAffinity<Affinity>();
            }
            CORO_HOT CORO_INLINE void await_resume() const noexcept {}
        };
        return FinalTransitionAwaiter{ this };
    }

    void unhandled_exception() noexcept
    {
        std::terminate();
    }
    
    template <ThreadAffinity NextAffinity, typename NextT>
    CORO_HOT CORO_INLINE auto await_transform(TaskBase<NextAffinity, NextT>&& next_task) noexcept
    {
        struct TransferAwaiter
        {
            Scheduler* scheduler_;
            TaskBase<NextAffinity, NextT> next_task_;

            CORO_HOT CORO_INLINE bool await_ready() const noexcept
            {
                return next_task_.done();
            }

            CORO_HOT auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
            {
                auto& promise = next_task_.handle_.promise();

                static constexpr auto resume_with_transfer =
                    [](Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<> {
                    return TransferPolicy<NextAffinity, Affinity>::transfer(scheduler, handle);
                };

                promise.continuation_handle_ = awaiting_coroutine;
                promise.continuation_fn_     = resume_with_transfer;

                return TransferPolicy<Affinity, NextAffinity>::transfer(scheduler_, next_task_.handle_);
            }

            CORO_HOT CORO_INLINE auto await_resume()
            {
                if constexpr (!std::is_void_v<NextT>)
                {
                    return next_task_.result();
                }
            }
        };
        return TransferAwaiter{ scheduler_, std::move(next_task) };
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return std::forward<AwaitableType>(awaitable);
    }
};

// Primary Promise template for NON-VOID tasks
template <ThreadAffinity Affinity, typename T>
struct Promise : public PromiseBase<Promise<Affinity, T>, Affinity, T>
{
    T value_{};

    template <typename U>
    void return_value(U&& value) noexcept(std::is_nothrow_move_assignable_v<U>)
    {
        value_ = std::forward<U>(value);
    }

    T& result() { return value_; }
};

// Promise specialization for VOID tasks
template <ThreadAffinity Affinity>
struct Promise<Affinity, void> : public PromiseBase<Promise<Affinity, void>, Affinity, void>
{
    void return_void() noexcept {}
};

} // namespace detail

// Unified TaskBase for both void and non-void return types
template <ThreadAffinity Affinity, typename T>
struct TaskBase
{
    using ResultType   = T;
    static constexpr ThreadAffinity affinity = Affinity;
    using promise_type = detail::Promise<Affinity, T>;

    TaskBase() noexcept = default;

    explicit TaskBase(std::coroutine_handle<promise_type> coroutine) noexcept
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

    CORO_HOT CORO_INLINE auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    template <typename U = T>
    CORO_HOT CORO_INLINE auto result() -> std::enable_if_t<!std::is_void_v<U>, U&>
    {
        return handle_.promise().result();
    }

    CORO_HOT CORO_INLINE auto await_ready() const noexcept -> bool
    {
        return done();
    }

    CORO_HOT CORO_INLINE auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> std::coroutine_handle<>
    {
        handle_.promise().continuation_handle_ = coroutine;
        // Fallback for generic awaitables - does not support cross-affinity returns.
        handle_.promise().continuation_fn_ = [](Scheduler* /*s*/, std::coroutine_handle<> h) { return h; };
        return handle_;
    }

    CORO_HOT CORO_INLINE auto await_resume()
    {
        if constexpr (!std::is_void_v<T>)
        {
            return result();
        }
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
