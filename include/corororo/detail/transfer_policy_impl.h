#pragma once

#include "enums.h"
#include "macros.h"
#include "scheduler.h"

namespace CoroRoro
{
namespace detail
{

//
// TransferPolicy Implementation
//
// A compile-time policy that defines how to transfer execution between coroutines.
//

template <ThreadAffinity CurrentAffinity, ThreadAffinity NextAffinity>
struct TransferPolicy final
{
    // This function is the key to symmetric transfer. By returning a coroutine handle from
    // `await_suspend`, we suspend the current coroutine and immediately resume the next
    // without growing the call stack.
    NO_DISCARD FORCE_INLINE HOT_PATH static auto transfer(Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
    {
        if constexpr (CurrentAffinity == NextAffinity)
        {
            // Fast Path: Same-affinity co_await.
            // Execution is transferred directly to the next coroutine, like a function call.
            return handle;
        }
        else
        {
            // Slow Path: Cross-affinity co_await.
            // 1. Schedule the next coroutine on its correct thread.
            scheduler->template scheduleHandleWithAffinity<NextAffinity>(handle);

            // 2. Symmetric Transfer: The current thread immediately gets a new task for itself
            //    to avoid becoming idle, maximizing throughput.
            return scheduler->template getNextTaskWithAffinity<CurrentAffinity>();
        }
    }
};

} // namespace detail
} // namespace CoroRoro
