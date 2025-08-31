#pragma once

#include <corororo/coroutine/awaiter.h>
#include <corororo/coroutine/execution_context.h>
#include <corororo/coroutine/task_state.h>
#include <corororo/coroutine/thread_affinity.h>
#include <corororo/coroutine/types.h>

#include <coroutine>
#include <functional>
#include <memory>
#include <type_traits>
#include <variant>

namespace CoroRoro
{

// Base promise with common logic, independent of return type.
template <ThreadAffinity Affinity, typename T>
struct PromiseTypeBase
{
    static constexpr ThreadAffinity affinity = Affinity;

    ExecutionContext*       context_         = nullptr;
    std::coroutine_handle<> continuation_    = nullptr;
    std::function<void()>   unblockCallerFn_ = nullptr;

    struct InitialAwaiter final
    {
        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        template <typename Promise>
        void await_suspend(std::coroutine_handle<Promise> h) const noexcept
        {
            auto& p = h.promise();

            // Only allocate new context if this is the root coroutine
            // Child coroutines will reference the parent's context
            if (!p.context_)
            {
                p.context_                  = new ExecutionContext();
                p.context_->activeAffinity_ = p.affinity;
            }

            p.context_->currentlyExecuting_ = h;
            p.context_->activeState_        = TaskState::Suspended;
        }

        void await_resume() const noexcept
        {
        }
    };

    auto initial_suspend() noexcept -> InitialAwaiter
    {
        return {};
    }

    struct FinalAwaiter final
    {
        auto await_ready() const noexcept -> bool
        {
            return false;
        }

        template <typename Promise>
        auto await_suspend(std::coroutine_handle<Promise> h) const noexcept -> std::coroutine_handle<>
        {
            auto& p = h.promise();

            if (p.unblockCallerFn_)
            {
                p.unblockCallerFn_();
                return std::noop_coroutine();
            }

            if (auto cont = p.continuation_)
            {
                p.context_->currentlyExecuting_ = cont;
                p.context_->activeState_        = TaskState::Suspended;
                p.context_->activeAffinity_     = p.affinity;

                return cont;
            }

            // Check if there's an exception and set appropriate state
            if constexpr (std::is_same_v<T, void>)
            {
                // void specialisation - check exception_ member
                if (p.exception_)
                {
                    p.context_->activeState_ = TaskState::Failed;
                }
                else
                {
                    p.context_->activeState_ = TaskState::Done;
                }
            }
            else
            {
                // value specialisation - check result_ variant
                if (std::holds_alternative<std::exception_ptr>(p.result_))
                {
                    p.context_->activeState_ = TaskState::Failed;
                }
                else
                {
                    p.context_->activeState_ = TaskState::Done;
                }
            }

            p.context_->isChainDone_ = true;

            return std::noop_coroutine();
        }

        void await_resume() const noexcept
        {
        }
    };

    auto final_suspend() noexcept -> FinalAwaiter
    {
        return {};
    }

    // This is called for every `co_await`. This is the key optimization point.
    template <ThreadAffinity AwaitedAffinity, typename AwaitedT>
    auto await_transform(CoroutineTask<AwaitedAffinity, AwaitedT>&& awaitable) noexcept
    {
        constexpr bool isSameAffinity = (affinity == AwaitedAffinity);

        // SIMPLE OPTIMIZATION: Every coroutine references the same context object
        // No shared_ptr overhead, no child tracking - just direct pointer sharing
        awaitable.handle_.promise().context_ = this->context_;

        return ConditionalTransferAwaiter<isSameAffinity, decltype(awaitable.handle_)>{ std::move(awaitable.handle_) };
    }

    // Explicitly delete yield_value to prevent co_yield
    template <typename U>
    void yield_value(U&&) = delete;
};

} // namespace CoroRoro
