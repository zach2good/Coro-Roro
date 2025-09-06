#pragma once

#include "enums.h"
#include "forward_declarations.h"
#include "scheduler_concept.h"
#include "task_aliases.h"
#include "task_base.h"

#include <coroutine>

namespace CoroRoro
{
namespace detail
{

//
// Inline Task Execution
//

template <typename T, ThreadAffinity Affinity>
inline auto runTaskInline(detail::TaskBase<Affinity, T>&& task)
{
    // Check if the task is already complete
    if (!task.handle_ || task.handle_.done())
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

    // Take ownership of the coroutine handle
    auto handle  = task.handle_;
    task.handle_ = nullptr; // Prevent destruction

    // Create inline scheduler that forces main thread execution
    detail::InlineScheduler inlineScheduler;

    // Use concept-based type safety to set the scheduler
    auto& promise = handle.promise();
    // Both Scheduler and InlineScheduler satisfy SchedulerLike concept,
    // so we can safely cast between them at runtime
    static_assert(detail::SchedulerLike<detail::InlineScheduler>, "InlineScheduler must satisfy SchedulerLike concept");
    static_assert(detail::SchedulerLike<Scheduler>, "Scheduler must satisfy SchedulerLike concept");
    promise.scheduler_ = reinterpret_cast<Scheduler*>(&inlineScheduler);

    // Execute the coroutine inline
    handle.resume();

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
        if (handle && handle.done())
        {
            T result = std::move(handle.promise().result());
            handle.destroy();
            return result;
        }
        else
        {
            // Task didn't complete, return default
            return T{};
        }
    }
}

} // namespace detail
} // namespace CoroRoro