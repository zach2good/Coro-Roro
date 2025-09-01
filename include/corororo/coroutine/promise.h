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
    constexpr bool await_ready() const noexcept
    {
        return false; // Always suspend on initial creation
    }
    constexpr void await_suspend(std::coroutine_handle<>) const noexcept
    {
    }
    constexpr void await_resume() const noexcept
    {
    }
};

// Type alias for continuation function pointer - avoids complex template delegation
using ContinuationFnPtr = std::coroutine_handle<> (*)(Scheduler*, std::coroutine_handle<>);

} // namespace detail

} // namespace CoroRoro