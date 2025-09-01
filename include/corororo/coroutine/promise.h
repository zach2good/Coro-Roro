#pragma once

#include <atomic>
#include <corororo/coroutine/types.h>
#include <coroutine>
#include <thread>
#include <type_traits>


namespace CoroRoro
{
// Forward declaration
class Scheduler;

namespace detail
{
// Final awaiter that handles coroutine completion
template <typename DerivedPromise>
struct FinalAwaiter final
{
    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    auto await_suspend(std::coroutine_handle<DerivedPromise> h) noexcept -> std::coroutine_handle<>
    {
        auto& promise = h.promise();

        // If there's a continuation, resume it
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

// Helper for await_suspend
template <typename DerivedPromise>
auto await_suspend(std::coroutine_handle<DerivedPromise> current,
                   std::coroutine_handle<>               awaited) noexcept -> std::coroutine_handle<>
{
    auto& promise         = current.promise();
    promise.continuation_ = awaited;
    return std::noop_coroutine();
}

// Conditional Transfer Awaiter - decides whether to transfer control directly or suspend
template <bool ShouldTransfer, typename AwaitedHandle>
struct ConditionalTransferAwaiter final
{
    AwaitedHandle handle_;

    auto await_ready() const noexcept -> bool
    {
        return false;
    }

    template <typename CurrentPromise>
    auto await_suspend(std::coroutine_handle<CurrentPromise> currentHandle) noexcept -> std::coroutine_handle<>
    {
        auto& child = handle_.promise();

        if constexpr (ShouldTransfer)
        {
            // Same scheduler - transfer control directly
            child.continuation_ = currentHandle;
            return handle_;
        }
        else
        {
            // Different scheduler or needs scheduling - suspend and schedule
            child.continuation_ = currentHandle;
            return std::noop_coroutine();
        }
    }

    auto await_resume()
    {
        if constexpr (requires { handle_.promise().result(); })
        {
            if constexpr (std::is_void_v<decltype(handle_.promise().result())>)
            {
                handle_.promise().result(); // Call for side effects only
            }
            else
            {
                return handle_.promise().result();
            }
        }
    }
};

// CRTP Base for all promise types - no virtual functions, pure compile-time polymorphism
template <typename Derived, typename T = void>
struct PromiseBase
{
    // Data storage for non-void types
    std::conditional_t<!std::is_void_v<T>, T, std::monostate> data_;

    Scheduler*              scheduler_      = nullptr;                // Reference to the scheduler
    std::coroutine_handle<> continuation_   = nullptr;                // Continuation coroutine
    ThreadAffinity          threadAffinity_ = ThreadAffinity::Worker; // Default to worker thread

    // CRTP helper - get derived instance
    auto derived() -> Derived&
    {
        return static_cast<Derived&>(*this);
    }

    auto derived() const -> const Derived&
    {
        return static_cast<const Derived&>(*this);
    }

    // This is called for every `co_await`. This is the key optimization point.
    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return derived().await_transform_impl(std::forward<AwaitableType>(awaitable));
    }

    // Default implementation - can be overridden by derived classes
    template <typename AwaitableType>
    auto await_transform_impl(AwaitableType&& awaitable) noexcept
    {
        // Check if this is a Task or AsyncTask (has handle_ member)
        if constexpr (requires { awaitable.handle_; })
        {
            // Share the same scheduler reference
            auto handle = awaitable.handle_;
            if (handle)
            {
                if (!handle.done())
                {
                    auto& promise      = handle.promise();
                    promise.scheduler_ = this->scheduler_;
                }
            }

            return std::forward<AwaitableType>(awaitable);
        }
        else
        {
            // This is not a Task/AsyncTask, let the compiler handle it normally
            return std::forward<AwaitableType>(awaitable);
        }
    }

    // Explicitly delete yield_value to prevent co_yield
    template <typename U>
    void yield_value(U&&) = delete;

    // Common methods that derived classes can use
    void unhandled_exception()
    {
        std::terminate();
    }
};

// Void specialization for CRTP PromiseBase
template <typename Derived>
struct PromiseBase<Derived, void>
{
    Scheduler*              scheduler_      = nullptr;                // Reference to the scheduler
    std::coroutine_handle<> continuation_   = nullptr;                // Continuation coroutine
    ThreadAffinity          threadAffinity_ = ThreadAffinity::Worker; // Default to worker thread

    // CRTP helper - get derived instance
    auto derived() -> Derived&
    {
        return static_cast<Derived&>(*this);
    }

    auto derived() const -> const Derived&
    {
        return static_cast<const Derived&>(*this);
    }

    // This is called for every `co_await`. This is the key optimization point.
    template <typename AwaitableType>
    auto await_transform(AwaitableType&& awaitable) noexcept
    {
        return derived().await_transform_impl(std::forward<AwaitableType>(awaitable));
    }

    // Default implementation - can be overridden by derived classes
    template <typename AwaitableType>
    auto await_transform_impl(AwaitableType&& awaitable) noexcept
    {
        // Check if this is a Task or AsyncTask (has handle_ member)
        if constexpr (requires { awaitable.handle_; })
        {
            // Share the same scheduler reference
            auto handle = awaitable.handle_;
            if (handle)
            {
                if (!handle.done())
                {
                    auto& promise      = handle.promise();
                    promise.scheduler_ = this->scheduler_;
                }
            }

            return std::forward<AwaitableType>(awaitable);
        }
        else
        {
            // This is not a Task/AsyncTask, let the compiler handle it normally
            return std::forward<AwaitableType>(awaitable);
        }
    }

    // Explicitly delete yield_value to prevent co_yield
    template <typename U>
    void yield_value(U&&) = delete;

    // Common methods that derived classes can use
    void unhandled_exception()
    {
        std::terminate();
    }
};

// CRTP Promise for Task types
template <typename TaskType, typename T = void>
struct TaskPromise : PromiseBase<TaskPromise<TaskType, T>, T>
{
    // Get the return object - TaskType is passed as template parameter
    auto get_return_object() noexcept -> TaskType
    {
        // Set thread affinity based on Task type
        this->threadAffinity_ = TaskType::affinity;
        return TaskType{ std::coroutine_handle<TaskPromise<TaskType, T>>::from_promise(*this) };
    }

    auto initial_suspend() const noexcept -> std::suspend_never
    {
        return {};
    }

    auto final_suspend() noexcept -> FinalAwaiter<TaskPromise<TaskType, T>>
    {
        return {};
    }

    void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        this->data_ = std::move(value);
    }

    void return_value(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        this->data_ = value;
    }

    auto result() -> T&
    {
        return this->data_;
    }
};

// Void specialization for CRTP TaskPromise
template <typename TaskType>
struct TaskPromise<TaskType, void> : PromiseBase<TaskPromise<TaskType, void>, void>
{
    // Get the return object - TaskType is passed as template parameter
    auto get_return_object() noexcept -> TaskType
    {
        // Set thread affinity based on Task type
        this->threadAffinity_ = TaskType::affinity;
        return TaskType{ std::coroutine_handle<TaskPromise<TaskType, void>>::from_promise(*this) };
    }

    auto initial_suspend() const noexcept -> std::suspend_never
    {
        return {};
    }

    auto final_suspend() noexcept -> FinalAwaiter<TaskPromise<TaskType, void>>
    {
        return {};
    }

    void return_void() noexcept
    {
    }
};

} // namespace detail

// AsyncTaskPromise definitions moved to async_task.h to avoid circular dependencies

} // namespace CoroRoro
