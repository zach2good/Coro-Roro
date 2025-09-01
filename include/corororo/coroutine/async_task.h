#pragma once

#include <corororo/coroutine/promise.h>
// Forward declare AsyncTask to avoid circular dependency
#include <corororo/coroutine/types.h>

#include <coroutine>
#include <exception>
#include <utility>

namespace CoroRoro
{

//
// AsyncTaskPromise for non-void types
//
template <typename T>
struct AsyncTaskPromise : detail::PromiseBase<T>
{
    AsyncTaskPromise()
    {
        this->threadAffinity_ = ThreadAffinity::Worker; // AsyncTasks run on worker threads
    }

    auto get_return_object() noexcept -> AsyncTask<T>
    {
        return AsyncTask<T>{std::coroutine_handle<AsyncTaskPromise<T>>::from_promise(*this)};
    }

    void return_value(T&& value) noexcept(std::is_nothrow_move_assignable_v<T>)
    {
        if constexpr (!std::is_void_v<T>) {
            this->data_ = std::move(value);
        }
    }

    void return_value(const T& value) noexcept(std::is_nothrow_copy_assignable_v<T>)
    {
        if constexpr (!std::is_void_v<T>) {
            this->data_ = value;
        }
    }

    auto result() -> std::conditional_t<!std::is_void_v<T>, T&, void>
    {
        if constexpr (!std::is_void_v<T>) {
            return this->data_;
        }
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

//
// AsyncTaskPromise<void> specialization
//
template <>
struct AsyncTaskPromise<void> : detail::PromiseBase<void>
{
    AsyncTaskPromise()
    {
        this->threadAffinity_ = ThreadAffinity::Worker; // AsyncTasks run on worker threads
    }

    auto get_return_object() noexcept -> AsyncTask<void>
    {
        return AsyncTask<void>{std::coroutine_handle<AsyncTaskPromise<void>>::from_promise(*this)};
    }

    void return_void() noexcept
    {
    }

    void result()
    {
        // void tasks don't have a result
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