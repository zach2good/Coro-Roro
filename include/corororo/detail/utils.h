#pragma once

#include "enums.h"
#include "forward_declarations.h"
#include "task_base.h"
#include "scheduler_concept.h"

#include <coroutine>

namespace CoroRoro
{
namespace detail
{

//
// Inline Task Execution
//

// Unified runTaskInline function that handles both Task<T> and AsyncTask<T>
// Uses if constexpr to handle different return types and affinities
template <typename T, ThreadAffinity Affinity>
inline auto runTaskInline(detail::TaskBase<Affinity, T>&& task)
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

    // For inline execution, we don't need to set a scheduler in the promise
    // The InlineScheduler handles task coordination locally
    auto& promise = handle.promise();
    promise.scheduler_ = nullptr; // No external scheduler needed for inline execution

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

} // namespace detail
} // namespace CoroRoro