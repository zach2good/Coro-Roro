#pragma once

#include <corororo/coroutine/task_state.h>
#include <corororo/coroutine/thread_affinity.h>
#include <corororo/coroutine/types.h>

#include <coroutine>
#include <functional>

namespace CoroRoro
{

// The Conditional Awaiter
// This awaiter is returned by await_transform. It decides whether to
// transfer control directly (same affinity) or suspend (different affinity).
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
        auto& parent = currentHandle.promise();
        auto& child  = handle_.promise();

        if constexpr (ShouldTransfer)
        {
            child.continuation_ = currentHandle;

            parent.context_->currentlyExecuting_ = handle_;
            parent.context_->activeAffinity_     = parent.affinity;
            parent.context_->activeState_        = TaskState::Suspended;

            return handle_;
        }
        else
        {
            auto parentAffinity = parent.affinity;

            child.unblockCallerFn_ = [ctx = parent.context_, cont = currentHandle, parentAffinity]()
            {
                ctx->currentlyExecuting_ = cont;
                ctx->activeState_        = TaskState::Suspended;
                ctx->activeAffinity_     = parentAffinity;
            };

            parent.context_->currentlyExecuting_ = handle_;
            parent.context_->activeState_        = TaskState::Suspended;
            parent.context_->activeAffinity_     = child.affinity;

            return std::noop_coroutine();
        }
    }

    auto await_resume()
    {
        return handle_.promise().getResult();
    }
};

} // namespace CoroRoro
