#pragma once

#include <corororo/coroutine/types.h>
#include <atomic>
#include <coroutine>
#include <thread>

namespace CoroRoro
{
// Forward declaration
class Scheduler;

namespace detail
{
    // Final awaiter that handles coroutine completion
    template <typename Promise>
    struct FinalAwaiter final
    {
        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        auto await_suspend(std::coroutine_handle<Promise> h) noexcept -> std::coroutine_handle<>
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
    template <typename Promise>
    auto await_suspend(std::coroutine_handle<Promise> current, 
                      std::coroutine_handle<> awaited) noexcept -> std::coroutine_handle<>
    {
        auto& promise = current.promise();
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

    // Base promise structure
    struct PromiseBase
    {
        Scheduler* scheduler_ = nullptr; // Reference to the scheduler
        std::coroutine_handle<> continuation_ = nullptr; // Continuation coroutine
        ThreadAffinity threadAffinity_ = ThreadAffinity::Worker; // Default to worker thread
        
        // This is called for every `co_await`. This is the key optimization point.
        template <typename AwaitableType>
        auto await_transform(AwaitableType&& awaitable) noexcept
        {
            // Check if this is a Task or AsyncTask (has getHandle method)
            if constexpr (requires { awaitable.getHandle(); })
            {
                // Share the same scheduler reference
                awaitable.getHandle().promise().scheduler_ = this->scheduler_;

                // Re-enable transfer control now that we have proper thread affinity routing
                constexpr bool shouldTransfer = true;

                return ConditionalTransferAwaiter<shouldTransfer, decltype(awaitable.getHandle())>{
                    awaitable.getHandle()
                };
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
    };

    // Promise for tasks that return a value
    template <typename Task, typename T>
    struct Promise final : public PromiseBase
    {
        T data_;

        auto get_return_object() noexcept -> Task
        {
            return Task{std::coroutine_handle<Promise>::from_promise(*this)};
        }

        auto initial_suspend() const noexcept -> std::suspend_never
        {
            return {};
        }

        auto final_suspend() noexcept -> FinalAwaiter<Promise>
        {
            return {};
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
    };

    // Promise for tasks that return void
    template <typename Task>
    struct Promise<Task, void> final : public PromiseBase
    {
        auto get_return_object() noexcept -> Task
        {
            return Task{std::coroutine_handle<Promise>::from_promise(*this)};
        }

        auto initial_suspend() const noexcept -> std::suspend_never
        {
            return {};
        }

        auto final_suspend() noexcept -> FinalAwaiter<Promise>
        {
            return {};
        }

        void return_void() noexcept
        {
        }

        void unhandled_exception()
        {
            std::terminate();
        }
    };

} // namespace detail
} // namespace CoroRoro
