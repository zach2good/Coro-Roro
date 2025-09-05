#pragma once

#include "cancellation_token.h"
#include "interval_task.h"
#include "scheduler.h"

namespace CoroRoro
{

//
// IntervalTask Implementation
//

inline IntervalTask::IntervalTask(Scheduler*                                                                        scheduler,
                                  std::function<CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void>()> factory,
                                  std::chrono::milliseconds                                                         interval,
                                  bool                                                                              isOneTime)
: factory_(std::move(factory))
, nextExecution_(std::chrono::steady_clock::now() - std::chrono::milliseconds(1))
, interval_(interval)
, scheduler_(scheduler)
, isOneTime_(isOneTime)
{
    // Add a small offset to ensure unique execution times for tasks created at the same time
    static std::atomic<int> counter{ 0 };
    nextExecution_ += std::chrono::microseconds(counter.fetch_add(1) % 1000);
}

inline IntervalTask::~IntervalTask()
{
    if (token_)
    {
        token_->clearTokenPointer();
    }
}

inline auto IntervalTask::createTrackedTask() -> std::optional<CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void>>
{
    // Check if already has an active child (single execution guarantee)
    bool expected = false;
    if (!hasActiveChild_.compare_exchange_strong(expected, true))
    {
        return std::nullopt; // Already has active child
    }

    // Create the task from factory
    auto task = factory_();

    // Task created by interval task factory

    // Return the task directly
    return task;
}

inline void IntervalTask::execute()
{
    try
    {
        if (cancelled_.load())
        {
            return;
        }

        // Try to create a tracked task
        auto task = createTrackedTask();
        if (!task)
        {
            // Single execution guarantee - already has active child
            return;
        }

        // Schedule the task for execution
        if (scheduler_)
        {
            // Set the scheduler pointer in the task's promise so it can notify completion
            task->handle().promise().scheduler_ = scheduler_;
            scheduler_->schedule(std::move(*task));
        }

        // Reset the active child flag (for now, until we implement proper tracking)
        hasActiveChild_.store(false);

        // Update next execution time only if not cancelled
        if (!cancelled_.load())
        {
            updateNextExecution();
        }
    }
    catch (...)
    {
        // For interval tasks, log the exception but don't propagate it
        // This allows interval tasks to continue running despite exceptions
        // Note: We don't have access to coro_log here, so we skip logging
    }
}

inline void IntervalTask::markCancelled()
{
    cancelled_.store(true);
}

inline auto IntervalTask::isCancelled() const -> bool
{
    return cancelled_.load();
}

inline auto IntervalTask::getNextExecution() const -> std::chrono::steady_clock::time_point
{
    return nextExecution_;
}

inline void IntervalTask::updateNextExecution()
{
    nextExecution_ += interval_;
}

inline void IntervalTask::setNextExecution(std::chrono::steady_clock::time_point time)
{
    nextExecution_ = time;
}

inline auto IntervalTask::isOneTime() const -> bool
{
    return isOneTime_;
}

inline void IntervalTask::setToken(CancellationToken* token)
{
    token_ = token;
}

inline void IntervalTask::clearTokenPointer()
{
    token_ = nullptr;
}

inline auto IntervalTask::createTrackedWrapper(CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void> originalTask) -> CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void>
{
    // Create a wrapper that executes the original task and marks completion
    return [this, originalTask = std::move(originalTask)]() -> CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void>
    {
        // Execute the original task by resuming it until it's done
        while (!originalTask.done())
        {
            originalTask.handle().resume();
        }

        // Mark that the child task has completed
        hasActiveChild_.store(false);
        co_return;
    }();
}

} // namespace CoroRoro
