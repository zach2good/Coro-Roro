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
// TODO: This currently doesn't do anything
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

    if (handle)
    {
        handle.destroy();
    }
    
    if constexpr (!std::is_void_v<T>)
    {
        return T{};
    }
}

} // namespace detail
} // namespace CoroRoro