#pragma once

#include "enums.h"
#include "forward_declarations.h"
#include "task_base.h"


#include <coroutine>

namespace CoroRoro
{
namespace detail
{

//
// Dummy Scheduler for Inline Execution
//

// Dummy scheduler implementation for inline execution of Task<void> only
class DummyScheduler
{
public:
    DummyScheduler()  = default;
    ~DummyScheduler() = default;

    void notifyTaskComplete()
    {
    }

    // Only handle Main affinity tasks since we only support Task<void>
    FORCE_INLINE HOT_PATH void scheduleHandleWithAffinity(std::coroutine_handle<> handle) noexcept
    {
        // Store the scheduled task to be returned by getNextTaskWithAffinity
        if (handle && !handle.done())
        {
            scheduledTask_ = handle;
        }
    }

    std::coroutine_handle<> getNextTaskWithAffinity()
    {
        // Return the scheduled task if we have one, otherwise return noop
        auto task      = scheduledTask_;
        scheduledTask_ = nullptr;
        return task ? task : std::noop_coroutine();
    }

    // Get a Scheduler* for promise compatibility
    Scheduler* asSchedulerPtr()
    {
        return reinterpret_cast<Scheduler*>(this);
    }

private:
    std::coroutine_handle<> scheduledTask_ = nullptr;
};

//
// Utility Functions
//

// Run a coroutine inline to completion without requiring a scheduler.
// This function works with Task<T> for any type T (Main affinity) and blocks until completion.
// AsyncTask types are not supported to keep the implementation simple.
// Returns the result of the coroutine (void for Task<void>, T for Task<T>).

template <typename T>
inline auto runCoroutineInline(Task<T>&& task)
{
    // Take ownership of the coroutine handle
    auto handle  = task.handle_;
    task.handle_ = nullptr; // Prevent destruction

    // Create a dummy scheduler to satisfy the FinalAwaiter
    DummyScheduler dummyScheduler;

    // Set the scheduler on the promise
    auto& promise      = handle.promise();
    promise.scheduler_ = dummyScheduler.asSchedulerPtr();

    // Simply resume the coroutine - the DummyScheduler will handle any scheduled tasks inline
    handle.resume();

    // Extract and return the result
    if constexpr (std::is_void_v<T>)
    {
        // For void tasks, just clean up and return
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
