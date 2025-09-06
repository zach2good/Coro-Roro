#pragma once

#include "enums.h"
#include "forward_declarations.h"
#include "macros.h"

#include <coroutine>
#include <type_traits>

namespace CoroRoro
{
namespace detail
{

//
// TaskBase
//
// A move-only, type-erased handle to a coroutine. This is the primary object that users
// will create and `co_await`. It is non-copyable to ensure clear ownership.
//

template <ThreadAffinity Affinity, typename T>
struct NO_DISCARD TaskBase final
{
    using ResultType = T;

    static constexpr ThreadAffinity affinity = Affinity;

    using promise_type = detail::Promise<Affinity, T>;

    TaskBase() noexcept = default;

    explicit TaskBase(std::coroutine_handle<promise_type> coroutine) noexcept
    : handle_(coroutine)
    {
    }

    TaskBase(TaskBase const&)            = delete;
    TaskBase& operator=(TaskBase const&) = delete;

    FORCE_INLINE TaskBase(TaskBase&& other) noexcept
    : handle_(other.handle_)
    {
        other.handle_ = nullptr;
    }

    FORCE_INLINE TaskBase& operator=(TaskBase&& other) noexcept
    {
        if (this != &other)
            LIKELY
            {
                if (handle_)
                {
                    handle_.destroy();
                }
                handle_       = other.handle_;
                other.handle_ = nullptr;
            }
        return *this;
    }

    FORCE_INLINE ~TaskBase() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    auto handle() const noexcept -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

    template <typename U = T>
    NO_DISCARD auto result() -> std::enable_if_t<!std::is_void_v<U>, U&>
    {
        return handle_.promise().result();
    }

    template <typename U = T>
    NO_DISCARD auto result() const -> std::enable_if_t<!std::is_void_v<U>, const U&>
    {
        return handle_.promise().result();
    }

    template <typename U = T>
    NO_DISCARD auto result() -> std::enable_if_t<std::is_void_v<U>>
    {
        return handle_.promise().result();
    }

    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    // This is a fallback for awaiting a raw TaskBase. The `await_transform` in the Promise
    // is the primary, more powerful mechanism for handling `co_await`.
    COLD_PATH auto await_suspend(std::coroutine_handle<> /* coroutine */) const noexcept -> std::coroutine_handle<>
    {
        // "fallback", lol.
        ASSERT_UNREACHABLE();

        // handle_.promise().continuation_ = detail::Continuation<Affinity, Affinity>{ coroutine };
        // return handle_;
    }

    auto await_resume() const
    {
        if constexpr (!std::is_void_v<T>)
        {
            return result();
        }
        else
        {
            // For void tasks, check if there's an exception to throw
            result();
        }
    }

public:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

} // namespace detail
} // namespace CoroRoro
