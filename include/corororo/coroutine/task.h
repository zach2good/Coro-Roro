#pragma once

#include <corororo/coroutine/promise.h>
#include <corororo/coroutine/task_state.h>
#include <corororo/coroutine/thread_affinity.h>
#include <corororo/coroutine/types.h>

#include <coroutine>
#include <exception>
#include <utility>
#include <variant>

namespace CoroRoro
{

// Specialisation for tasks that return a value.
template <ThreadAffinity Affinity, typename T>
struct CoroutineTask final
{
    using ResultType = T;

    struct PromiseType final : PromiseTypeBase<Affinity, T>
    {
        std::variant<std::monostate, T, std::exception_ptr> result_;

        auto get_return_object() -> CoroutineTask
        {
            return { std::coroutine_handle<PromiseType>::from_promise(*this) };
        }

        void unhandled_exception()
        {
            this->result_.template emplace<std::exception_ptr>(std::current_exception());
        }

        void return_value(T&& value)
        {
            this->result_.template emplace<T>(std::move(value));
        }

        void return_value(const T& value)
        {
            this->result_.template emplace<T>(value);
        }

        auto getResult() -> T&
        {
            if (std::holds_alternative<std::exception_ptr>(this->result_))
            {
                std::rethrow_exception(std::get<std::exception_ptr>(this->result_));
            }
            return std::get<T>(this->result_);
        }
    };

    using promise_type = PromiseType;

    std::coroutine_handle<PromiseType> handle_;

    auto resume() -> TaskState
    {
        if (done())
        {
            return state();
        }

        handle_.promise().context_->currentlyExecuting_.resume();

        return state();
    }

    auto done() const -> bool
    {
        return handle_.promise().context_->isChainDone_;
    }

    auto state() const -> TaskState
    {
        return handle_.promise().context_->activeState_;
    }

    auto threadAffinity() const -> ThreadAffinity
    {
        return handle_.promise().context_->activeAffinity_;
    }

    auto getResult() -> T&
    {
        return handle_.promise().getResult();
    }

    auto get_result() -> T&
    {
        return getResult();
    }

    ~CoroutineTask()
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    CoroutineTask(std::coroutine_handle<PromiseType> h)
    : handle_(h)
    {
    }

    CoroutineTask(CoroutineTask&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    CoroutineTask(const CoroutineTask&)            = delete;
    CoroutineTask& operator=(const CoroutineTask&) = delete;
    CoroutineTask& operator=(CoroutineTask&&)      = delete;
};

// Specialisation for tasks that return void.
template <ThreadAffinity Affinity>
struct CoroutineTask<Affinity, void> final
{
    using ResultType = void;

    struct PromiseType final : PromiseTypeBase<Affinity, void>
    {
        std::exception_ptr exception_ = nullptr;

        auto get_return_object() -> CoroutineTask
        {
            return { std::coroutine_handle<PromiseType>::from_promise(*this) };
        }

        void unhandled_exception()
        {
            exception_ = std::current_exception();
        }

        void return_void()
        {
        }

        void getResult()
        {
            if (exception_)
            {
                std::rethrow_exception(exception_);
            }
        }
    };

    using promise_type = PromiseType;

    std::coroutine_handle<PromiseType> handle_;

    auto resume() -> TaskState
    {
        if (done())
        {
            return state();
        }

        handle_.promise().context_->currentlyExecuting_.resume();

        return state();
    }

    auto done() const -> bool
    {
        return handle_.promise().context_->isChainDone_;
    }

    auto state() const -> TaskState
    {
        return handle_.promise().context_->activeState_;
    }

    auto threadAffinity() const -> ThreadAffinity
    {
        return handle_.promise().context_->activeAffinity_;
    }

    void getResult() const
    {
        handle_.promise().getResult();
    }

    void get_result() const
    {
        getResult();
    }

    ~CoroutineTask()
    {
        if (handle_)
        {
            handle_.destroy();
        }
    }

    CoroutineTask(std::coroutine_handle<PromiseType> h)
    : handle_(h)
    {
    }

    CoroutineTask(CoroutineTask&& other) noexcept
    : handle_(std::exchange(other.handle_, nullptr))
    {
    }

    CoroutineTask(const CoroutineTask&)            = delete;
    CoroutineTask& operator=(const CoroutineTask&) = delete;
    CoroutineTask& operator=(CoroutineTask&&)      = delete;
};

} // namespace CoroRoro