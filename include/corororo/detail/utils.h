#pragma once

#include "enums.h"
#include "forward_declarations.h"
#include "task_base.h"
#include "task_aliases.h"
#include "scheduler_concept.h"

#include <coroutine>

namespace CoroRoro
{
namespace detail
{

//
// Inline Task Execution
//

template <typename T, ThreadAffinity Affinity>
inline void runTaskInline(detail::TaskBase<Affinity, T>&& task)
{
    // To facilitate this inline task execution, we need to create a lightweight and simple
    // scheduler that forces main thread execution.
    detail::InlineScheduler inlineScheduler;
    inlineScheduler.schedule(std::move(task));
    inlineScheduler.runExpiredTasks();
}

} // namespace detail
} // namespace CoroRoro