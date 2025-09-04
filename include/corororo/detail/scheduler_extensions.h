#pragma once

#include "scheduler.h"
#include "task_aliases.h"

namespace CoroRoro
{

//
// Additional Scheduler Methods
//

// Additional schedule method for void-returning callables
// Automatically wraps the callable in a coroutine for execution
template <typename Callable>
void Scheduler::schedule(Callable&& callable)
    requires std::is_invocable_v<Callable> &&
             std::is_void_v<std::invoke_result_t<Callable>>
{
    // Wrap the void-returning callable in a coroutine
    auto wrappedTask = [callable = std::forward<Callable>(callable)]() -> Task<void>
    {
        callable();
        co_return;
    };
    schedule(wrappedTask);
}

} // namespace CoroRoro
