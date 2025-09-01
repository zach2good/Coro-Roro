#pragma once

#include <corororo/coroutine/task.hpp>
#include <coroutine>

namespace CoroRoro
{

// Simple awaiter for conditional transfer
template <bool SameAffinity, typename HandleType>
struct ConditionalTransferAwaiter
{
    HandleType handle;

    bool await_ready() const noexcept
    {
        return false;
    }

    void await_suspend(std::coroutine_handle<> coroutine) const noexcept
    {
        if constexpr (SameAffinity)
        {
            // Same affinity - resume immediately
            coroutine.resume();
        }
        else
        {
            // Different affinity - schedule on different thread
            // This will be handled by the scheduler
        }
    }

    auto await_resume() const noexcept
    {
        return handle;
    }
};

} // namespace CoroRoro
