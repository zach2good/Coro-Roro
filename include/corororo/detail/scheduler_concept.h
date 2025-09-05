#pragma once

#include "enums.h"
#include "macros.h"

#include <coroutine>
#include <concepts>
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
concept SchedulerLike = requires(T& scheduler, std::coroutine_handle<> handle, ThreadAffinity affinity)
{
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

// =====================================================================================
// INLINE SCHEDULER - SIMPLIFIED
// =====================================================================================
// A special scheduler that forces all execution to happen on the main thread
// with no queues. Perfect for inline execution of tasks from start to finish.

class InlineScheduler
{
private:
    // Store the most recently scheduled task for inline execution
    std::coroutine_handle<> scheduledTask_ = nullptr;

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
        }
    }

    template <ThreadAffinity Affinity>
    auto getNextTaskWithAffinity() noexcept -> std::coroutine_handle<>
    {
        // Return and clear the scheduled task
        auto task = scheduledTask_;
        scheduledTask_ = nullptr;
        return task ? task : std::noop_coroutine();
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
            auto task = scheduledTask_;
            scheduledTask_ = nullptr;
            task.resume();
        }
    }
};

// =====================================================================================
// INLINE EXECUTION FUNCTIONS
// =====================================================================================
//
// Standalone function to execute a Task from start to finish inline
//
// This function creates an InlineScheduler and executes the task immediately,
// forcing all affinity changes to run on the main thread with no queues.
//

template <typename T>
inline auto runTaskInline(detail::TaskBase<ThreadAffinity::Main, T>&& task)
{
    // Take ownership of the coroutine handle
    auto handle = task.handle_;
    task.handle_ = nullptr; // Prevent destruction

    if (!handle || handle.done())
    {
        // Task is already complete or invalid
        if constexpr (!std::is_void_v<T>)
        {
            return T{}; // Return default value for non-void types
        }
        else
        {
            return; // For void, just return
        }
    }

    // Create inline scheduler that forces main thread execution
    detail::InlineScheduler inlineScheduler;

    // Set scheduler on promise (required for coroutine system to work)
    auto& promise = handle.promise();
    promise.scheduler_ = reinterpret_cast<Scheduler*>(&inlineScheduler);

    // Execute the coroutine inline
    handle.resume();

    // Execute any pending tasks (like co_awaited subtasks) inline
    while (inlineScheduler.hasPendingTask())
    {
        inlineScheduler.executePendingTask();
    }

    // Extract and return the result
    if constexpr (std::is_void_v<T>)
    {
        // For void tasks, just clean up
        if (handle)
        {
            handle.destroy();
        }
    }
    else
    {
        // For non-void tasks, extract the result before cleanup
        T result = std::move(handle.promise().result());

        // Clean up the coroutine handle
        if (handle)
        {
            handle.destroy();
        }

        return result;
    }
}

//
// Helper function for void tasks (convenience overload)
//

inline void runTaskInline(detail::TaskBase<ThreadAffinity::Main, void>&& task)
{
    runTaskInline<void>(std::move(task));
}

//
// Overloads for AsyncTask (Worker affinity) - for testing mixed Task/AsyncTask chains
//

template <typename T>
inline auto runTaskInline(detail::TaskBase<ThreadAffinity::Worker, T>&& task)
{
    // Take ownership of the coroutine handle
    auto handle = task.handle_;
    task.handle_ = nullptr; // Prevent destruction

    if (!handle || handle.done())
    {
        // Task is already complete or invalid
        if constexpr (!std::is_void_v<T>)
        {
            return T{}; // Return default value for non-void types
        }
        else
        {
            return; // For void, just return
        }
    }

    // Create inline scheduler that forces main thread execution
    detail::InlineScheduler inlineScheduler;

    // Set scheduler on promise (required for coroutine system to work)
    auto& promise = handle.promise();
    promise.scheduler_ = reinterpret_cast<Scheduler*>(&inlineScheduler);

    // Execute the coroutine inline
    handle.resume();

    // Execute any pending tasks (like co_awaited subtasks) inline
    while (inlineScheduler.hasPendingTask())
    {
        inlineScheduler.executePendingTask();
    }

    // Extract and return the result
    if constexpr (std::is_void_v<T>)
    {
        // For void tasks, just clean up
        if (handle)
        {
            handle.destroy();
        }
    }
    else
    {
        // For non-void tasks, extract the result before cleanup
        T result = std::move(handle.promise().result());

        // Clean up the coroutine handle
        if (handle)
        {
            handle.destroy();
        }

        return result;
    }
}

//
// Helper function for void AsyncTask (convenience overload)
//

inline void runTaskInline(detail::TaskBase<ThreadAffinity::Worker, void>&& task)
{
    runTaskInline<void>(std::move(task));
}

} // namespace detail
} // namespace CoroRoro
