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

inline auto CancellationToken::isCancelled() const -> bool
{
    return cancelled_.load();
}

inline CancellationToken::operator bool() const
{
    return valid();
}

inline void CancellationToken::setTask(IntervalTask* task)
{
    task_ = task;
}

inline void CancellationToken::clearTokenPointer()
{
    task_ = nullptr;
}

} // namespace CoroRoro
