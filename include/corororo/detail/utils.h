#pragma once

#include "task_base.h"
#include "enums.h"
#include "forward_declarations.h"

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
    DummyScheduler() = default;
    ~DummyScheduler() = default;

    void notifyTaskComplete() {}

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
        auto task = scheduledTask_;
        scheduledTask_ = nullptr;
        return task ? task : std::noop_coroutine();
    }

    // Convert to Scheduler* for promise compatibility (safe because we control the usage)
    operator Scheduler*() { return reinterpret_cast<Scheduler*>(this); }

private:
    std::coroutine_handle<> scheduledTask_ = nullptr;
};

//
// Utility Functions
//

// Run a coroutine inline to completion without requiring a scheduler.
// This function only works with Task<void> (Main affinity) and blocks until completion.
// AsyncTask types are not supported to keep the implementation simple.

inline void runCoroutineInline(Task<void>&& task)
{
    // Take ownership of the coroutine handle
    auto handle = task.handle_;
    task.handle_ = nullptr; // Prevent destruction

    if (!handle || handle.done())
    {
        return; // Nothing to do
    }

    // Create a dummy scheduler to satisfy the FinalAwaiter
    DummyScheduler dummyScheduler;

    // Set the scheduler on the promise (using conversion operator for type safety)
    auto& promise = handle.promise();
    promise.scheduler_ = dummyScheduler; // Uses DummyScheduler::operator Scheduler*()

    // Simply resume the coroutine - the DummyScheduler will handle any scheduled tasks inline
    handle.resume();

    // Clean up the original coroutine handle
    if (handle)
    {
        handle.destroy();
    }
}

} // namespace detail
} // namespace CoroRoro
