#pragma once

#include "continuation.h"
#include "enums.h"
#include "forward_declarations.h"
#include "macros.h"
#include "transfer_policy.h"

#include <coroutine>

namespace CoroRoro
{
namespace detail
{

//
// InitialAwaiter
//
// Ensures a newly created coroutine *always* suspends immediately. This gives the `schedule`
// function control of the coroutine handle before it begins execution, preventing it from
// running eagerly on the caller's stack.
//

struct InitialAwaiter final
{
    constexpr bool await_ready() const noexcept
    {
        return false; // Always suspend (hand over to await_suspend)
    }
    constexpr void await_suspend(std::coroutine_handle<>) const noexcept
    {
    }
    constexpr void await_resume() const noexcept
    {
    }
};

//
// FinalAwaiter
//
// This awaiter is the key to chaining coroutines and performing symmetric transfer. When a
// coroutine completes, it either resumes its parent or asks the scheduler for a new task.
//

template <ThreadAffinity Affinity, typename Promise>
struct FinalAwaiter final
{
    Promise* promise_;

    FORCE_INLINE bool await_ready() const noexcept
    {
        return false; // Always suspend (hand over to await_suspend)
    }

    NO_DISCARD HOT_PATH std::coroutine_handle<> await_suspend(std::coroutine_handle<> /* self */) const noexcept
    {
        return std::visit(
            [&](auto&& continuation) noexcept -> std::coroutine_handle<>
            {
                using TContinuation = std::decay_t<decltype(continuation)>;
                if constexpr (std::is_same_v<TContinuation, std::monostate>)
                {
                    // This was a top-level task. It has finished.
                    // Check if the coroutine completed with an exception
                    // Cast to derived promise type to access result_
                    auto* derivedPromise = static_cast<typename Promise::DerivedPromise*>(promise_);
                    if (std::holds_alternative<std::exception_ptr>(derivedPromise->result_))
                    {
                        // Always re-throw exceptions for natural propagation
                        // The scheduler will catch and handle them appropriately
                        std::rethrow_exception(std::get<std::exception_ptr>(derivedPromise->result_));
                    }

                    if (promise_->scheduler_)
                    {
                        promise_->scheduler_->notifyTaskComplete();

                        // Symmetric Transfer on Task Completion:
                        // No continuation exists (a top-level task). Ask the scheduler for the next
                        // available task for this thread to execute immediately.
                        return promise_->scheduler_->template getNextTaskWithAffinity<Affinity>();
                    }
                    else
                    {
                        // Inline execution - no external scheduler
                        return std::noop_coroutine();
                    }
                }
                else
                {
                    // Resume Parent Coroutine:
                    // A continuation exists. Resume it using the `Continuation` object, which
                    // contains the correct `TransferPolicy`.
                    return continuation.resume(promise_->scheduler_);
                }
            },
            promise_->continuation_);
    }

    FORCE_INLINE void await_resume() const noexcept
    {
    }
};

} // namespace detail
} // namespace CoroRoro
