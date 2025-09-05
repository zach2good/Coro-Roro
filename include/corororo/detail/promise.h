#pragma once

#include "awaiters.h"
#include "continuation.h"
#include "enums.h"
#include "forward_declarations.h"
#include "macros.h"
#include "transfer_policy.h"

#include <coroutine>
#include <type_traits>

namespace CoroRoro
{
namespace detail
{

//
// PromiseBase
//
// The promise is the core component of a coroutine, managing its state and interactions with the
// scheduler. It provides the necessary hooks for suspension, resumption, and exception handling.
// It also provides storage for the coroutine's state and result.
//

template <typename Derived, ThreadAffinity Affinity, typename T>
struct PromiseBase
{
    Scheduler*          scheduler_{ nullptr };
    ContinuationVariant continuation_{};
    std::exception_ptr  unhandledException_{ nullptr };

    static constexpr ThreadAffinity affinity = Affinity;

    NO_DISCARD FORCE_INLINE auto get_return_object() noexcept
    {
        return detail::TaskBase<Affinity, T>{
            std::coroutine_handle<Derived>::from_promise(*static_cast<Derived*>(this))
        };
    }

    FORCE_INLINE auto initial_suspend() const noexcept
    {
        return InitialAwaiter{};
    }

    FORCE_INLINE auto final_suspend() noexcept
    {
        return FinalAwaiter<Affinity, PromiseBase>{ this };
    }

    COLD_PATH void unhandled_exception()
    {
        // Store the exception for later propagation
        unhandledException_ = std::current_exception();
    }

    //
    // await_transform
    //
    // This critical customization point is called on `co_await`. It creates a special awaiter that:
    // 1. Stores the current coroutine's handle as a continuation in the `next_task`.
    // 2. Uses `TransferPolicy` to immediately transfer execution to `next_task` or a new task.
    //
    template <ThreadAffinity NextAffinity, typename NextT>
    HOT_PATH auto await_transform(detail::TaskBase<NextAffinity, NextT>&& nextTask) noexcept
    {
        struct TransferAwaiter final
        {
            using promise_type = typename detail::TaskBase<NextAffinity, NextT>::promise_type;

            Scheduler*                          scheduler_;
            std::coroutine_handle<promise_type> handle_;

            // Explicit constructor to handle initialisation from the enclosing function.
            TransferAwaiter(Scheduler* scheduler, detail::TaskBase<NextAffinity, NextT>&& task) noexcept
            : scheduler_(scheduler)
            , handle_(task.handle_)
            {
                // Before we can co_await the next task, we must propagate the scheduler pointer.
                // This ensures that when the next task completes, its `final_suspend` can correctly
                // interact with the scheduler to resume the continuation or fetch a new task.
                handle_.promise().scheduler_ = scheduler_;

                // Release the handle from the task to prevent double destruction
                task.handle_ = nullptr;
            }

            // This awaiter is move-only, as it contains a coroutine handle.
            // Explicitly deleting copy operations silences MSVC warning C4625.
            // Defaulting move operations silences MSVC warning C4626.
            TransferAwaiter(const TransferAwaiter&)            = delete;
            TransferAwaiter& operator=(const TransferAwaiter&) = delete;
            TransferAwaiter(TransferAwaiter&&)                 = default;
            TransferAwaiter& operator=(TransferAwaiter&&)      = default;

            FORCE_INLINE bool await_ready() const noexcept
            {
                return !handle_ || handle_.done();
            }

            NO_DISCARD HOT_PATH FORCE_INLINE auto await_suspend(std::coroutine_handle<> awaiting_coroutine) noexcept -> std::coroutine_handle<>
            {
                // Store this coroutine's handle as the continuation for the next task,
                // preserving the parent's affinity information for the eventual resume.
                handle_.promise().continuation_ = detail::Continuation<NextAffinity, Affinity>{ awaiting_coroutine };

                // Perform the "downward" transfer to start executing next_task. `TransferPolicy`
                // will either return `handle_` directly (same affinity) or schedule
                // it and return a new task for this thread (different affinity).
                return TransferPolicy<Affinity, NextAffinity>::transfer(scheduler_, handle_);
            }

            FORCE_INLINE auto await_resume() const
            {
                // Check if the child coroutine had an unhandled exception
                if (handle_.promise().unhandledException_)
                {
                    // Re-throw the exception to propagate it up the chain
                    std::rethrow_exception(handle_.promise().unhandledException_);
                }

                if constexpr (!std::is_void_v<NextT>)
                {
                    return handle_.promise().result();
                }
            }
        };
        return TransferAwaiter{ scheduler_, std::move(nextTask) };
    }

    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return std::forward<AwaitableType>(awaitable);
    }
};

//
// Promise for <T>
//

template <ThreadAffinity Affinity, typename T>
struct Promise final : public PromiseBase<Promise<Affinity, T>, Affinity, T>
{
    T value_{};

    FORCE_INLINE void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        value_ = std::move(value);
    }

    FORCE_INLINE void return_value(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        value_ = value;
    }

    FORCE_INLINE auto result() noexcept -> T&
    {
        return value_;
    }

    FORCE_INLINE auto result() const noexcept -> const T&
    {
        return value_;
    }
};

//
// Promise for <void>
//

template <ThreadAffinity Affinity>
struct Promise<Affinity, void> final : public PromiseBase<Promise<Affinity, void>, Affinity, void>
{
    FORCE_INLINE void return_void() noexcept
    {
    }
};

} // namespace detail
} // namespace CoroRoro
