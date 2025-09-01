#pragma once

#include <corororo/coroutine/promise.hpp>
#include <corororo/coroutine/task.hpp>

#include <coroutine>
#include <exception>
#include <utility>

namespace CoroRoro
{

// AsyncTask is now defined as a type alias in types.h
// This file only contains the promise definitions needed for compilation

//
// AsyncTaskPromise for non-void types
//
template <typename T>
struct AsyncTaskPromise final : public detail::PromiseBase
{
    T data_;

    auto get_return_object() noexcept -> Task<T>
    {
        return {std::coroutine_handle<AsyncTaskPromise>::from_promise(*this)};
    }

    void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        data_ = std::move(value);
    }

    void return_value(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        data_ = value;
    }

    auto result() -> T&
    {
        return data_;
    }

    auto initial_suspend() const noexcept -> std::suspend_always
    {
        return {};
    }

    auto final_suspend() noexcept -> detail::FinalAwaiter<AsyncTaskPromise>
    {
        return {};
    }

    void unhandled_exception()
    {
        std::terminate();
    }
};

// Forward declare AsyncTaskPromise<void> specialization
template <>
struct AsyncTaskPromise<void>;

//
// AsyncTaskPromise<void> specialization
//
template <>
struct AsyncTaskPromise<void> final : public detail::PromiseBase
{
    auto get_return_object() noexcept -> Task<void>
    {
        return {std::coroutine_handle<AsyncTaskPromise>::from_promise(*this)};
    }

    void return_void() noexcept
    {
    }

    void result()
    {
        // For void tasks, result() does nothing
    }

    auto initial_suspend() const noexcept -> std::suspend_always
    {
        return {};
    }

    auto final_suspend() noexcept -> detail::FinalAwaiter<AsyncTaskPromise<void>>
    {
        return {};
    }

    void unhandled_exception()
    {
        std::terminate();
    }
};

} // namespace CoroRoro