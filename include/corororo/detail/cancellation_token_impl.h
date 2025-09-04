#pragma once

#include "cancellation_token.h"
#include "interval_task.h"

namespace CoroRoro
{

//
// CancellationToken Implementation
//

inline CancellationToken::CancellationToken(IntervalTask* task, Scheduler* scheduler)
: task_(task)
, scheduler_(scheduler)
{
    if (task_)
    {
        task_->setToken(this);
    }
}

inline CancellationToken::~CancellationToken()
{
    if (task_)
    {
        task_->clearTokenPointer();
        task_->markCancelled();
    }
}

inline CancellationToken::CancellationToken(CancellationToken&& other) noexcept
: task_(other.task_)
, scheduler_(other.scheduler_)
, cancelled_(other.cancelled_.load())
{
    other.task_      = nullptr;
    other.scheduler_ = nullptr;
    other.cancelled_.store(false);

    // Update the task's token pointer
    if (task_)
    {
        task_->setToken(this);
    }
}

inline CancellationToken& CancellationToken::operator=(CancellationToken&& other) noexcept
{
    if (this != &other)
    {
        // Clean up current task
        if (task_)
        {
            task_->clearTokenPointer();
            task_->markCancelled();
        }

        // Move from other token
        task_      = other.task_;
        scheduler_ = other.scheduler_;
        cancelled_.store(other.cancelled_.load());

        // Clear other token
        other.task_      = nullptr;
        other.scheduler_ = nullptr;
        other.cancelled_.store(false);

        // Update the task's token pointer
        if (task_)
        {
            task_->setToken(this);
        }
    }
    return *this;
}

inline CancellationToken& CancellationToken::operator=(const CancellationToken& other)
{
    if (this != &other)
    {
        // Clean up current task
        if (task_)
        {
            task_->clearTokenPointer();
            task_->markCancelled();
        }

        // Copy from other token
        task_      = other.task_;
        scheduler_ = other.scheduler_;
        cancelled_.store(other.cancelled_.load());

        // Update the task's token pointer
        if (task_)
        {
            task_->setToken(this);
        }
    }
    return *this;
}

inline void CancellationToken::cancel()
{
    cancelled_.store(true);
    if (task_)
    {
        task_->markCancelled();
    }
}

inline auto CancellationToken::valid() const -> bool
{
    return !cancelled_.load() && task_ != nullptr;
}

inline void CancellationToken::clearTokenPointer()
{
    task_ = nullptr;
}

} // namespace CoroRoro
