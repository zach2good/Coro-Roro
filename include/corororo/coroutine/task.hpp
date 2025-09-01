#pragma once

#include <corororo/coroutine/promise.hpp>
#include <corororo/coroutine/types.hpp>
#include <coroutine>
#include <utility>

namespace CoroRoro
{

//
// Task
//
//   A coroutine task that executes immediately on the current thread.
//   Uses std::suspend_never for immediate execution.
//
template <typename T = void>
class Task final
{
public:
    using promise_type = detail::Promise<Task, T>;
    using ResultType = T;

    Task() noexcept = default;
    
    Task(std::coroutine_handle<promise_type> coroutine) noexcept
        : handle_{coroutine}
    {
    }
    
    Task(Task const&) = delete;
    Task& operator=(Task const&) = delete;
    
    Task(Task&& other) noexcept
        : handle_{other.handle_}
    {
        other.handle_ = nullptr;
    }
    
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
            {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    ~Task() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    // Get the underlying coroutine handle for scheduler access
    auto getHandle() const noexcept -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

    // Check if the task is done
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    // Get the result (only valid after the task is done)
    auto result() -> T&
    {
        return handle_.promise().result();
    }

    // Get the result (const version)
    auto result() const -> const T&
    {
        return handle_.promise().result();
    }

    // Legacy method for backward compatibility
    auto get_result() -> T&
    {
        return result();
    }

    // Legacy method for backward compatibility
    auto get_result() const -> const T&
    {
        return result();
    }

    // Awaitable interface
    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> std::coroutine_handle<>
    {
        return detail::await_suspend(handle_, coroutine);
    }

    auto await_resume() -> T&
    {
        return result();
    }

private:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

// Specialization for void Task
template <>
class Task<void> final
{
public:
    using promise_type = detail::Promise<Task<void>, void>;
    using ResultType = void;

    Task() noexcept = default;
    
    Task(std::coroutine_handle<promise_type> coroutine) noexcept
        : handle_{coroutine}
    {
    }
    
    Task(Task const&) = delete;
    Task& operator=(Task const&) = delete;
    
    Task(Task&& other) noexcept
        : handle_{other.handle_}
    {
        other.handle_ = nullptr;
    }
    
    Task& operator=(Task&& other) noexcept
    {
        if (this != &other)
        {
            if (handle_)
            {
                handle_.destroy();
            }
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }
    
    ~Task() noexcept
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    // Get the underlying coroutine handle for scheduler access
    auto getHandle() const noexcept -> std::coroutine_handle<promise_type>
    {
        return handle_;
    }

    // Check if the task is done
    auto done() const noexcept -> bool
    {
        return !handle_ || handle_.done();
    }

    // Awaitable interface
    auto await_ready() const noexcept -> bool
    {
        return done();
    }

    auto await_suspend(std::coroutine_handle<> coroutine) noexcept -> std::coroutine_handle<>
    {
        return detail::await_suspend(handle_, coroutine);
    }

    void await_resume() const noexcept
    {
        // void tasks don't return a value
    }

private:
    std::coroutine_handle<promise_type> handle_ = nullptr;
};

} // namespace CoroRoro
