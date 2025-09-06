#pragma once

#include "enums.h"
#include "macros.h"

#include <chrono>
#include <concepts>
#include <coroutine>
#include <variant>

namespace CoroRoro
{
namespace detail
{

//
// Scheduler Concept
//
// Defines the interface that any scheduler type must implement to be used
// with the coroutine system. This allows for zero-cost abstraction without
// virtual dispatch overhead.
//

template <typename T>
concept SchedulerLike = requires(T& scheduler, std::coroutine_handle<> handle, ThreadAffinity affinity) {
    // Must be able to notify when a task completes
    { scheduler.notifyTaskComplete() } -> std::same_as<void>;

    // Must be able to schedule a handle with specific affinity
    { scheduler.template scheduleHandleWithAffinity<ThreadAffinity::Main>(handle) } -> std::same_as<void>;
    { scheduler.template scheduleHandleWithAffinity<ThreadAffinity::Worker>(handle) } -> std::same_as<void>;

    // Must be able to get next task with specific affinity
    { scheduler.template getNextTaskWithAffinity<ThreadAffinity::Main>() } -> std::same_as<std::coroutine_handle<>>;
    { scheduler.template getNextTaskWithAffinity<ThreadAffinity::Worker>() } -> std::same_as<std::coroutine_handle<>>;
};

//
// Concept-based Dummy Scheduler
//
// A no-op scheduler implementation that satisfies the SchedulerLike concept.
// Useful for testing or when you want to run coroutines without actual scheduling.
//

class ConceptDummyScheduler
{
public:
    // Notify that a task has completed (no-op for dummy scheduler)
    FORCE_INLINE void notifyTaskComplete() noexcept
    {
        // No-op: dummy scheduler doesn't track task completion
    }

    // Schedule a handle with specific affinity (no-op for dummy scheduler)
    template <ThreadAffinity Affinity>
    FORCE_INLINE void scheduleHandleWithAffinity(std::coroutine_handle<> /* handle */) noexcept
    {
        // No-op: dummy scheduler doesn't actually schedule anything
    }

    // Get next task with specific affinity (always returns noop_coroutine)
    template <ThreadAffinity Affinity>
    FORCE_INLINE auto getNextTaskWithAffinity() noexcept -> std::coroutine_handle<>
    {
        return std::noop_coroutine();
    }
};

static_assert(SchedulerLike<ConceptDummyScheduler>, "ConceptDummyScheduler does not satisfy SchedulerLike concept");

//
// Inline Scheduler
//
// A special scheduler that forces all execution to happen on the main thread
// with no queues. Perfect for inline execution of tasks from start to finish.
//

class InlineScheduler
{
private:
    // Store the most recently scheduled task for inline execution
    std::coroutine_handle<> scheduledTask_ = nullptr;
    // Store the currently executing task for inline resumption
    std::coroutine_handle<> executingTask_ = nullptr;

public:
    void notifyTaskComplete() noexcept
    {
        // No-op for inline execution
    }

    template <ThreadAffinity Affinity>
    void scheduleHandleWithAffinity(std::coroutine_handle<> handle) noexcept
    {
        // Store the handle for inline execution - it will be resumed immediately
        // by the runTaskInline function after the initial resume
        if (handle && !handle.done())
        {
            scheduledTask_ = handle;
            executingTask_ = handle;
        }
    }

    template <ThreadAffinity Affinity>
    auto getNextTaskWithAffinity() noexcept -> std::coroutine_handle<>
    {
        // For inline execution, if we have an executing task that's not done, return it
        if (executingTask_ && !executingTask_.done())
        {
            return executingTask_;
        }

        // Otherwise, return and clear the scheduled task
        auto task      = scheduledTask_;
        scheduledTask_ = nullptr;
        if (task && !task.done())
        {
            executingTask_ = task;
            return task;
        }

        return std::noop_coroutine();
    }

    // Check if there's a task waiting to be executed
    bool hasPendingTask() const noexcept
    {
        return scheduledTask_ != nullptr;
    }

    // Execute any pending task inline
    void executePendingTask()
    {
        if (scheduledTask_ && !scheduledTask_.done())
        {
            auto task      = scheduledTask_;
            scheduledTask_ = nullptr;
            task.resume();
        }
    }

    // Schedule a task for inline execution
    template <ThreadAffinity Affinity, typename T>
    void schedule(detail::TaskBase<Affinity, T>&& task)
    {
        auto handle  = task.handle_;
        task.handle_ = nullptr; // Prevent destruction
        if (handle && !handle.done())
        {
            // Set the scheduler on the promise for proper task coordination
            handle.promise().scheduler_ = reinterpret_cast<Scheduler*>(this);
            scheduledTask_ = handle;
            executingTask_ = handle;
        }
    }

    // Run expired tasks (for InlineScheduler, execute the scheduled task)
    auto runExpiredTasks() -> std::chrono::milliseconds
    {
        auto start = std::chrono::steady_clock::now();

        if (scheduledTask_ && !scheduledTask_.done())
        {
            auto task = scheduledTask_;
            scheduledTask_ = nullptr;
            executingTask_ = task;
            task.resume();

            // If the task is done, clean it up
            if (task.done())
            {
                executingTask_ = nullptr;
                task.destroy();
            }
        }

        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }
};

static_assert(SchedulerLike<InlineScheduler>, "InlineScheduler does not satisfy SchedulerLike concept");

} // namespace detail
} // namespace CoroRoro
