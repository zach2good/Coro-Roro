#pragma once

#include <corororo/coroutine/types.h>
#include <coroutine>
#include <type_traits>

namespace CoroRoro {

// Forward declaration
class Scheduler;

// Forward declaration for TaskBase
template <ThreadAffinity Affinity, typename T>
struct TaskBase;

// TransferPolicy - static methods to avoid complex template instantiation chains
template <ThreadAffinity CurrentAffinity, ThreadAffinity NextAffinity>
struct TransferPolicy
{
    [[nodiscard]] static auto transfer(Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>;
};

namespace detail {

// Simple awaiter for initial suspension - no complex template chains
struct InitialAwaiter
{
    CORO_HOT CORO_INLINE bool await_ready() const noexcept
    {
        return true; // Don't suspend - let coroutine execute immediately
    }
    CORO_HOT CORO_INLINE void await_suspend(std::coroutine_handle<>) const noexcept
    {
    }
    CORO_HOT CORO_INLINE void await_resume() const noexcept
    {
    }
};

// Type alias for continuation function pointer - avoids complex template delegation
using ContinuationFnPtr = std::coroutine_handle<> (*)(Scheduler*, std::coroutine_handle<>);

} // namespace detail

} // namespace CoroRoro