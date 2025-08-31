#pragma once

#include <corororo/coroutine/task.h>
#include <corororo/coroutine/thread_affinity.h>
#include <corororo/coroutine/types.h>
#include <corororo/scheduler/cancellation_token.h>
#include <corororo/scheduler/delayed_task.h>
#include <corororo/scheduler/forced_affinity_task.h>
#include <corororo/scheduler/forced_thread_affinity.h>
#include <corororo/scheduler/interval_task.h>
#include <corororo/scheduler/schedulable_task.h>
#include <corororo/scheduler/scheduled_task.h>
#include <corororo/scheduler/worker_pool.h>
#include <corororo/util/macros.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include <concurrentqueue/concurrentqueue.h>

namespace CoroRoro
{

//
// Forward declarations
//

class CancellationToken;

//
// Scheduler
//
//   Modern coroutine scheduler that replaces CTaskManager.
//
//   Features:
//   - Immediate task execution (schedule)
//   - Interval/delayed tasks with cancellation tokens
//   - Thread affinity control
//   - Factory pattern for reusable interval tasks
//   - RAII resource management
//
class Scheduler final
{
    friend class WorkerPool; // Allow WorkerPool to access private queueTask method

    // Allow task wrapper classes to access private methods
    template <typename T>
    friend class DelayedTask;

    template <typename T>
    friend class IntervalTask;

public:
    // Type aliases for clarity
    using milliseconds = std::chrono::milliseconds;
    using time_point   = std::chrono::time_point<std::chrono::steady_clock>;
    using steady_clock = std::chrono::steady_clock;
    using TaskId       = std::uint64_t;

    explicit Scheduler(size_t       numThreads             = std::max(1U, std::thread::hardware_concurrency() - 1U),
                       milliseconds destructionGracePeriod = milliseconds(250));

    ~Scheduler();

    MOVE_ONLY(Scheduler);

    //
    // Immediate execution scheduling
    //

    template <typename TaskType>
    void schedule(TaskType&& task);

    template <typename TaskType>
    void schedule(ForcedThreadAffinity affinity, TaskType&& task);

    //
    // Cancellable task scheduling
    //

    template <typename FactoryType>
    auto scheduleInterval(milliseconds interval, FactoryType&& factory,
                          milliseconds firstExecutionDelay = milliseconds::zero()) -> CancellationToken;

    template <typename FactoryType>
    auto scheduleInterval(ForcedThreadAffinity affinity, milliseconds interval, FactoryType&& factory,
                          milliseconds firstExecutionDelay = milliseconds::zero()) -> CancellationToken;

    template <typename TaskType>
    auto scheduleDelayed(milliseconds delay, TaskType&& task) -> CancellationToken;

    template <typename TaskType>
    auto scheduleDelayed(ForcedThreadAffinity affinity, milliseconds delay, TaskType&& task) -> CancellationToken;

    template <typename TaskType>
    auto scheduleAt(time_point when, TaskType&& task) -> CancellationToken;

    template <typename TaskType>
    auto scheduleAt(ForcedThreadAffinity affinity, time_point when, TaskType&& task) -> CancellationToken;

    //
    // Main thread execution
    //

    auto runExpiredTasks(time_point referenceTime = steady_clock::now()) -> milliseconds;

private:
    //
    // Helper methods
    //

    void executeOrRoute(/* ScheduledTask scheduledTask */);
    void executeIntervalTask(/* ScheduledTask scheduledTask */);
    void routeTask(/* ScheduledTask scheduledTask */);
    void notifyMainThread();
    void processTaskQueue();
    void processExpiredTasks(time_point referenceTime);
    void scheduleTaskAt(time_point when, std::unique_ptr<ISchedulableTask> task);
    void addInFlightTask(TaskId taskId);
    void removeInFlightTask(TaskId taskId);
    void markTaskAsRescheduled(TaskId taskId);
    bool isTaskRescheduled(TaskId taskId) const;

    // Cancellation management
    bool isTaskCancelled(TaskId taskId);

public:
    // CancellationToken needs access to this
    void cancelTask(TaskId taskId);

    // WorkerPool needs access to this (used by friend class)
    void queueTask(std::unique_ptr<ISchedulableTask> task);

    // Helper method for testing - get count of in-flight tasks
    size_t getInFlightTaskCount() const;

private:
    // Task lifecycle management
    std::atomic<TaskId>        nextTaskId_{ 1 };
    std::unordered_set<TaskId> inFlightTasks_;
    std::unordered_set<TaskId> cancelledTasks_;
    std::unordered_set<TaskId> rescheduledTasks_;    // Tracks tasks that have been rescheduled (don't remove from inFlightTasks)
    std::unordered_set<TaskId> activeIntervalTasks_; // Tracks interval tasks that are currently executing
    mutable std::mutex         taskTrackingMutex_;

    std::thread::id mainThreadId_;
    milliseconds    destructionGracePeriod_;

    // Thread pool for worker tasks
    std::unique_ptr<WorkerPool> workerPool_;

    //
    // Timed task system
    //

    // Task wrapper classes are now in separate headers for better organization

    // LOCK-FREE QUEUES: Replace mutex-protected queues with moodycamel::ConcurrentQueue
    // This eliminates the highest contention points in the scheduler
    
    moodycamel::ConcurrentQueue<ScheduledTask> timedTaskQueue_;
    // Note: timedTaskMutex_ removed - no longer needed for lock-free queue

    // Task queue for main thread immediate execution
    moodycamel::ConcurrentQueue<std::unique_ptr<ISchedulableTask>> mainThreadQueue_;
    // Note: mainThreadMutex_ removed - no longer needed for lock-free queue
};

//
// Template implementations
//

template <typename TaskType>
void Scheduler::schedule(TaskType&& task)
{
    if constexpr (std::is_invocable_v<TaskType>)
    {
        // Handle callables that return coroutines or regular functions
        if constexpr (requires { task().resume(); })
        {
            // Callable returns a coroutine
            auto coro    = task();
            auto wrapped = std::make_unique<SchedulableTaskWrapper<decltype(coro)>>(std::move(coro));
            queueTask(std::move(wrapped));
        }
        else
        {
            // Regular callable - create a simple wrapper coroutine
            auto coro = [task = std::forward<TaskType>(task)]() mutable -> Task<void>
            {
                task();
                co_return;
            }();
            auto wrapped = std::make_unique<SchedulableTaskWrapper<decltype(coro)>>(std::move(coro));
            queueTask(std::move(wrapped));
        }
    }
    else
    {
        // Direct coroutine object
        if constexpr (requires { task.resume(); })
        {
            auto wrapped = std::make_unique<SchedulableTaskWrapper<TaskType>>(std::forward<TaskType>(task));
            queueTask(std::move(wrapped));
        }
    }
}

template <typename TaskType>
void Scheduler::schedule(ForcedThreadAffinity affinity, TaskType&& task)
{
    // Convert ForcedThreadAffinity to ThreadAffinity
    ThreadAffinity targetAffinity = (affinity == ForcedThreadAffinity::MainThread)
                                        ? ThreadAffinity::MainThread
                                        : ThreadAffinity::WorkerThread;

    // Handle callables vs direct coroutine objects
    if constexpr (std::is_invocable_v<TaskType>)
    {
        // Handle callables that return coroutines or regular functions
        if constexpr (requires { task().resume(); })
        {
            // Callable returns a coroutine
            auto coro       = task();
            auto forcedTask = std::make_unique<ForcedAffinityTask<decltype(coro)>>(
                targetAffinity, std::move(coro));
            queueTask(std::move(forcedTask));
        }
        else
        {
            // Regular callable - create a simple wrapper coroutine
            auto coro = [task = std::forward<TaskType>(task)]() mutable -> Task<void>
            {
                task();
                co_return;
            }();
            auto forcedTask = std::make_unique<ForcedAffinityTask<decltype(coro)>>(
                targetAffinity, std::move(coro));
            queueTask(std::move(forcedTask));
        }
    }
    else
    {
        // Direct coroutine object
        if constexpr (requires { task.resume(); })
        {
            auto forcedTask = std::make_unique<ForcedAffinityTask<TaskType>>(
                targetAffinity, std::forward<TaskType>(task));
            queueTask(std::move(forcedTask));
        }
    }
}

template <typename FactoryType>
auto Scheduler::scheduleInterval(ForcedThreadAffinity affinity, Scheduler::milliseconds interval, FactoryType&& factory,
                                 Scheduler::milliseconds firstExecutionDelay) -> CancellationToken
{
    static_assert(false, "scheduleInterval with ForcedThreadAffinity is not implemented yet");
    return CancellationToken{};
}

template <typename TaskType>
auto Scheduler::scheduleDelayed(Scheduler::milliseconds delay, TaskType&& task) -> CancellationToken
{
    // Generate unique task ID
    TaskId taskId = nextTaskId_.fetch_add(1);

    // Add to in-flight tasks
    addInFlightTask(taskId);

    // Handle callables vs direct coroutine objects
    if constexpr (std::is_invocable_v<TaskType>)
    {
        // Callable that returns a coroutine or regular function
        if constexpr (requires { task().resume(); })
        {
            // Callable returns a coroutine
            auto coro        = task();
            auto delayedTask = std::make_unique<DelayedTask<decltype(coro)>>(
                *this, taskId, std::move(coro));

            // Schedule for delayed execution
            auto executionTime = steady_clock::now() + delay;
            scheduleTaskAt(executionTime, std::move(delayedTask));
        }
        else
        {
            // Regular callable - wrap in Task<void>
            auto coro = [task = std::forward<TaskType>(task)]() mutable -> Task<void>
            {
                task();
                co_return;
            }();
            auto delayedTask = std::make_unique<DelayedTask<decltype(coro)>>(
                *this, taskId, std::move(coro));

            // Schedule for delayed execution
            auto executionTime = steady_clock::now() + delay;
            scheduleTaskAt(executionTime, std::move(delayedTask));
        }
    }
    else
    {
        // Direct coroutine object
        auto delayedTask = std::make_unique<DelayedTask<TaskType>>(
            *this, taskId, std::forward<TaskType>(task));

        // Schedule for delayed execution
        auto executionTime = steady_clock::now() + delay;
        scheduleTaskAt(executionTime, std::move(delayedTask));
    }

    // Return cancellation token
    return CancellationToken{ this, taskId };
}

template <typename TaskType>
auto Scheduler::scheduleDelayed(ForcedThreadAffinity affinity, Scheduler::milliseconds delay, TaskType&& task) -> CancellationToken
{
    static_assert(false, "scheduleDelayed with ForcedThreadAffinity is not implemented yet");
    return CancellationToken{};
}

template <typename TaskType>
auto Scheduler::scheduleAt(Scheduler::time_point when, TaskType&& task) -> CancellationToken
{
    static_assert(false, "scheduleAt with absolute time is not implemented yet");
    return CancellationToken{};
}

template <typename TaskType>
auto Scheduler::scheduleAt(ForcedThreadAffinity affinity, Scheduler::time_point when, TaskType&& task) -> CancellationToken
{
    static_assert(false, "scheduleAt with ForcedThreadAffinity is not implemented yet");
    return CancellationToken{};
}

//
// Non-template implementations
//

inline Scheduler::Scheduler(size_t numThreads, milliseconds destructionGracePeriod)
: mainThreadId_(std::this_thread::get_id())
, destructionGracePeriod_(destructionGracePeriod)
, workerPool_(std::make_unique<WorkerPool>(numThreads, *this))
{
}

inline Scheduler::~Scheduler() = default;

inline auto Scheduler::runExpiredTasks(time_point referenceTime) -> milliseconds
{
    auto start = steady_clock::now();

    // Process expired timed tasks first
    processExpiredTasks(referenceTime);

    // Then process immediate tasks
    processTaskQueue();

    return std::chrono::duration_cast<milliseconds>(steady_clock::now() - start);
}

inline void Scheduler::processExpiredTasks(time_point referenceTime)
{
    // LOCK-FREE: Process tasks from moodycamel::ConcurrentQueue
    // Since we can't use priority queue operations, we need to handle ordering differently
    
    std::vector<ScheduledTask> expiredTasks;
    std::vector<ScheduledTask> nonExpiredTasks;
    
    // Extract all tasks from the lock-free queue
    ScheduledTask dequeuedTask;
    while (timedTaskQueue_.try_dequeue(dequeuedTask))
    {
        if (dequeuedTask.nextExecution <= referenceTime)
        {
            // Task is expired - execute it
            expiredTasks.push_back(std::move(dequeuedTask));
        }
        else
        {
            // Task is not expired - re-queue it
            nonExpiredTasks.push_back(std::move(dequeuedTask));
        }
    }
    
    // Re-queue non-expired tasks (they'll be processed in FIFO order, not priority order)
    // TODO: This is a temporary solution - we need a proper lock-free priority queue for optimal performance
    for (auto& task : nonExpiredTasks)
    {
        timedTaskQueue_.enqueue(std::move(task));
    }
    
    // Execute expired tasks
    for (auto& expiredTask : expiredTasks)
    {
        try
        {
            TaskState state = expiredTask.task->resume();

            if (state == TaskState::Suspended)
            {
                // Task suspended - re-queue based on its current affinity
                queueTask(std::move(expiredTask.task));
            }
            // If completed or failed, task is destroyed automatically
        }
        catch (...)
        {
            // TODO: Add proper error handling
        }
    }
}

inline void Scheduler::scheduleTaskAt(time_point when, std::unique_ptr<ISchedulableTask> task)
{
    // LOCK-FREE: Use moodycamel::ConcurrentQueue::enqueue instead of mutex-protected priority queue
    // Note: This changes from priority queue to FIFO queue - we'll need to handle ordering differently
    timedTaskQueue_.enqueue({ when, std::move(task) });
}

inline bool Scheduler::isTaskCancelled(TaskId taskId)
{
    std::lock_guard<std::mutex> lock(taskTrackingMutex_);
    return cancelledTasks_.find(taskId) != cancelledTasks_.end();
}

inline void Scheduler::cancelTask(TaskId taskId)
{
    std::lock_guard<std::mutex> lock(taskTrackingMutex_);

    // Only mark as cancelled if the task is actually in-flight
    if (inFlightTasks_.find(taskId) != inFlightTasks_.end())
    {
        cancelledTasks_.insert(taskId);
    }
    // If task is not in-flight, ignore the cancellation request
}

inline void Scheduler::addInFlightTask(TaskId taskId)
{
    std::lock_guard<std::mutex> lock(taskTrackingMutex_);
    inFlightTasks_.insert(taskId);
}

inline void Scheduler::removeInFlightTask(TaskId taskId)
{
    std::lock_guard<std::mutex> lock(taskTrackingMutex_);
    inFlightTasks_.erase(taskId);
    cancelledTasks_.erase(taskId); // Clean up cancelled task tracking too
}

inline void Scheduler::markTaskAsRescheduled(TaskId taskId)
{
    std::lock_guard<std::mutex> lock(taskTrackingMutex_);
    rescheduledTasks_.insert(taskId);
}

inline bool Scheduler::isTaskRescheduled(TaskId taskId) const
{
    std::lock_guard<std::mutex> lock(taskTrackingMutex_);
    return rescheduledTasks_.find(taskId) != rescheduledTasks_.end();
}

inline size_t Scheduler::getInFlightTaskCount() const
{
    std::lock_guard<std::mutex> lock(taskTrackingMutex_);
    return inFlightTasks_.size();
}

inline void Scheduler::queueTask(std::unique_ptr<ISchedulableTask> task)
{
    ThreadAffinity affinity = task->threadAffinity();

    if (affinity == ThreadAffinity::MainThread)
    {
        // LOCK-FREE: Use moodycamel::ConcurrentQueue::enqueue instead of mutex-protected queue
        mainThreadQueue_.enqueue(std::move(task));
    }
    else if (affinity == ThreadAffinity::WorkerThread)
    {
        workerPool_->enqueueTask(std::move(task));
    }
    else
    {
        // Default to main thread for unknown affinity
        // LOCK-FREE: Use moodycamel::ConcurrentQueue::enqueue instead of mutex-protected queue
        mainThreadQueue_.enqueue(std::move(task));
    }
}

inline void Scheduler::processTaskQueue()
{
    // Process main thread tasks using lock-free queue
    // LOCK-FREE: Process tasks directly from moodycamel::ConcurrentQueue without swapping
    
    std::unique_ptr<ISchedulableTask> task;
    
    // Process all available tasks from the lock-free queue
    while (mainThreadQueue_.try_dequeue(task))
    {
        try
        {
            TaskState state = task->resume();

            if (state == TaskState::Suspended)
            {
                // Task suspended - re-queue based on its current affinity
                queueTask(std::move(task));
            }
            // If completed or failed, task is destroyed automatically
        }
        catch (...)
        {
            // TODO: Add proper error handling
        }
    }
}

template <typename FactoryType>
auto Scheduler::scheduleInterval(milliseconds interval, FactoryType&& factory, milliseconds firstExecutionDelay) -> CancellationToken
{
    // Generate unique task ID
    TaskId taskId = nextTaskId_.fetch_add(1);

    // Add to in-flight tasks
    addInFlightTask(taskId);

    // Create interval task
    auto intervalTask = std::make_unique<IntervalTask<FactoryType>>(
        *this, taskId, interval, std::forward<FactoryType>(factory));

    // Schedule first execution
    auto firstExecution = steady_clock::now() + firstExecutionDelay;
    scheduleTaskAt(firstExecution, std::move(intervalTask));

    // Return cancellation token
    return CancellationToken{ this, taskId };
}

//
// CancellationToken implementation (after Scheduler is fully defined)
//

inline void CancellationToken::cancel()
{
    if (!scheduler_ || !isValid())
    {
        return; // Already cancelled or invalid
    }

    bool expectedFalse = false;
    if (!cancelled_.compare_exchange_strong(expectedFalse, true))
    {
        // Double-cancellation protection (log warning as specified in design)
        // TODO: Add proper logging framework
        return; // Already cancelled
    }
    scheduler_->cancelTask(taskId_);
}

//
// Task wrapper implementations that need complete Scheduler definition
//

template <typename TaskType>
inline auto DelayedTask<TaskType>::resume() -> TaskState
{
    // Check if cancelled via scheduler's internal tracking
    if (scheduler_.isTaskCancelled(taskId_))
    {
        // Remove from in-flight when cancelled
        scheduler_.removeInFlightTask(taskId_);
        return TaskState::Done; // Task was cancelled
    }

    // Execute the task directly
    TaskState result = task_.resume();

    // If task is done, remove from in-flight (unless it was rescheduled)
    if (result == TaskState::Done)
    {
        if (!scheduler_.isTaskRescheduled(taskId_))
        {
            scheduler_.removeInFlightTask(taskId_);
        }
        else
        {
            // Task was rescheduled, clear the rescheduled flag but keep it in-flight
            std::lock_guard<std::mutex> lock(scheduler_.taskTrackingMutex_);
            scheduler_.rescheduledTasks_.erase(taskId_);
        }
    }

    return result;
}

template <typename FactoryType>
inline auto IntervalTask<FactoryType>::resume() -> TaskState
{
    // Check if cancelled via scheduler's internal tracking
    if (scheduler_.isTaskCancelled(taskId_))
    {
        // Remove from in-flight and active interval tasks when cancelled
        scheduler_.removeInFlightTask(taskId_);
        {
            std::lock_guard<std::mutex> lock(scheduler_.taskTrackingMutex_);
            scheduler_.activeIntervalTasks_.erase(taskId_);
        }
        return TaskState::Done; // Don't reschedule
    }

    // If we don't have a child task yet, create one and mark as active
    if (!childTask_)
    {
        // Check if this interval task is already active (prevents multiple instances)
        {
            std::lock_guard<std::mutex> lock(scheduler_.taskTrackingMutex_);
            if (scheduler_.activeIntervalTasks_.find(taskId_) != scheduler_.activeIntervalTasks_.end())
            {
                // Another instance of this interval task is already running, skip this one
                return TaskState::Done;
            }
            scheduler_.activeIntervalTasks_.insert(taskId_);
        }

        auto childTaskCoroutine = factory_();
        childTask_              = std::make_unique<SchedulableTaskWrapper<decltype(childTaskCoroutine)>>(std::move(childTaskCoroutine));
        childTaskStartTime_     = std::chrono::steady_clock::now();
    }

    // Execute the child task
    if (childTask_)
    {
        TaskState childState = childTask_->resume();

        if (childState == TaskState::Suspended)
        {
            // Child task is suspended, we need to suspend too and continue later
            return TaskState::Suspended;
        }

        // Child task completed (either Done or Failed)
        childTask_.reset(); // Clean up completed child task (sets to nullptr)

        // Calculate next execution time based on when the child task completed
        // This ensures consistent intervals between task completions, not starts
        auto now = std::chrono::steady_clock::now();
        
        // Calculate the next execution time based on when this task was supposed to run
        // For a 400ms interval, we want executions at 0ms, 400ms, 800ms, etc.
        // Always use the standard interval to maintain consistent timing
        auto nextTime = childTaskStartTime_ + interval_;
        
        // CRITICAL: If the next time is in the past (task took longer than interval),
        // schedule it to run immediately to catch up
        if (nextTime <= now) {
            nextTime = now + std::chrono::milliseconds(1); // Schedule almost immediately
        }

        // CRITICAL: Remove this task from activeIntervalTasks_ BEFORE creating the new instance
        // This prevents the new instance from thinking another instance is running
        {
            std::lock_guard<std::mutex> lock(scheduler_.taskTrackingMutex_);
            scheduler_.activeIntervalTasks_.erase(taskId_);
        }
        
        // CRITICAL: Remove the old task from inFlightTasks_ since we're creating a new one
        scheduler_.removeInFlightTask(taskId_);

        // Reschedule this factory task for next interval
        // Create a copy of the factory since the original may be in a moved-from state
        FactoryType factoryCopy = factory_;
        auto selfCopy = std::make_unique<IntervalTask>(scheduler_, taskId_, interval_, std::move(factoryCopy));
        
        // CRITICAL: Add the rescheduled task to in-flight tracking
        scheduler_.addInFlightTask(taskId_);
        scheduler_.scheduleTaskAt(nextTime, std::move(selfCopy));

        return TaskState::Done; // This factory task instance is complete
    }

    // Should never reach here
    return TaskState::Done;
}

//
// WorkerPool implementation that needs complete Scheduler definition
//

inline void WorkerPool::routeTaskToScheduler(std::unique_ptr<ISchedulableTask> task)
{
    scheduler_.queueTask(std::move(task));
}

} // namespace CoroRoro
