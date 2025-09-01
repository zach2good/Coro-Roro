#pragma once

#include <corororo/coroutine/task.h>
#include <corororo/coroutine/types.h>
#include <corororo/scheduler/worker_pool.h>
#include <corororo/scheduler/scheduled_task.h>
#include <corororo/scheduler/cancellation_token.h>
#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <queue>

namespace CoroRoro
{

// Forward declarations
class WorkerPool;

//
// SchedulerConcept
//
//   Concept that defines the interface for any scheduler implementation.
//
template <typename S>
concept SchedulerConcept = requires(S scheduler,
                                   std::coroutine_handle<> coroutine)
{
    scheduler.scheduleHandle(coroutine);
};

//
// Scheduler
//
//   Main scheduler class implementing handle-based scheduling.
//   Manages worker threads and distributes coroutine execution.
//
// Type aliases for API compatibility
//
using milliseconds = std::chrono::milliseconds;
using time_point   = std::chrono::time_point<std::chrono::steady_clock>;
using steady_clock = std::chrono::steady_clock;

//
// Scheduler
//
class Scheduler final
{
public:
    explicit Scheduler(size_t numThreads = std::max(1U, std::thread::hardware_concurrency() - 1U));
    ~Scheduler() noexcept;

    Scheduler(Scheduler const&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler const&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    // Schedule method for Task types (main thread) - uses compile-time affinity
    template <typename T>
    void schedule(Task<T>&& task)
    {
        if (task.handle_)
        {
            // Propagate scheduler reference to the task's promise
            task.handle_.promise().scheduler_ = this;
            scheduleHandleWithAffinity(task.handle_, Task<T>::affinity);
        }
    }

    // Schedule method for AsyncTask types (worker threads) - uses compile-time affinity
    template <typename T>
    void schedule(AsyncTask<T>&& task)
    {
        if (task.handle_)
        {
            // Propagate scheduler reference to the task's promise
            task.handle_.promise().scheduler_ = this;
            scheduleHandleWithAffinity(task.handle_, AsyncTask<T>::affinity);
        }
    }

    // Generic schedule method for any task type - uses compile-time affinity when available
    template <typename TaskType>
    void schedule(TaskType&& task)
    {
        // Extract the coroutine handle and schedule it
        if constexpr (requires { task.handle_; })
        {
            auto handle = task.handle_;
            if (handle)
            {
                // Propagate scheduler reference to the task's promise
                handle.promise().scheduler_ = this;

                // Use compile-time affinity if available, otherwise determine at runtime
                if constexpr (requires { TaskType::affinity; })
                {
                    scheduleHandleWithAffinity(handle, TaskType::affinity);
                }
                else
                {
                    scheduleHandle(handle);
                }
            }
        }
    }

    // Process expired scheduled tasks and return processing time
    auto runExpiredTasks(time_point referenceTime = steady_clock::now()) -> milliseconds
    {
        auto start = steady_clock::now();

        // Process main thread tasks
        while (!mainThreadTasks_.empty())
        {
            auto handle = mainThreadTasks_.back();
            mainThreadTasks_.pop_back();

            if (handle && !handle.done())
            {
                // Resume the coroutine on the main thread
                handle.resume();
            }
        }

        // Process expired scheduled tasks
        {
            std::lock_guard<std::mutex> lock(scheduledTasksMutex_);
            std::queue<std::shared_ptr<ScheduledTask>> remainingTasks;

            while (!scheduledTasks_.empty())
            {
                auto task = scheduledTasks_.front();
                scheduledTasks_.pop();

                if (task->shouldExecute(referenceTime))
                {
                    // For interval tasks, check if one is already active
                    // Temporarily disable to debug SEH exception
                    /*
                    if (task->getType() == ScheduledTask::Type::Interval && isIntervalTaskActive(task))
                    {
                        // Interval task is already active, skip execution and reschedule
                        remainingTasks.push(task);
                        continue;
                    }

                    // Mark interval task as active before execution
                    if (task->getType() == ScheduledTask::Type::Interval)
                    {
                        markIntervalTaskActive(task);
                    }
                    */

                    // Execute the task
                    auto nextTime = task->execute();

                    if (task->getType() == ScheduledTask::Type::Interval && nextTime != time_point::max())
                    {
                        // For interval tasks, reschedule with new execution time
                        auto rescheduledTask = std::make_shared<ScheduledTask>(
                            ScheduledTask::Type::Interval,
                            nextTime,
                            task->getTaskFactory(),
                            task->getToken()
                        );
                        rescheduledTask->setInterval(task->getInterval());
                        remainingTasks.push(rescheduledTask);
                    }
                    // One-time tasks are discarded after execution
                }
                else if (!task->isCancelled())
                {
                    // Keep non-expired, non-cancelled tasks
                    remainingTasks.push(task);
                }
                // Cancelled tasks are discarded
            }

            scheduledTasks_ = std::move(remainingTasks);
        }

        return std::chrono::duration_cast<milliseconds>(steady_clock::now() - start);
    }

    // Schedule a task to execute after a delay
    template <typename Callable>
    auto scheduleDelayed(milliseconds delay, Callable&& callable) -> CancellationToken
    {
        CancellationToken token;

        // Store callable for later execution using std::function to handle lambdas with captures
        auto storedCallable = std::make_shared<std::function<Task<void>()>>(std::forward<Callable>(callable));

        // Create delayed task
        auto scheduledTask = std::make_shared<ScheduledTask>(
            ScheduledTask::Type::Delayed,
            steady_clock::now() + delay,
            [storedCallable, this]() -> Task<void> {
                // Call the stored callable to get a Task
                auto task = (*storedCallable)();

                // Schedule the resulting task
                if constexpr (requires { task.handle_; })
                {
                    auto handle = task.handle_;
                    if (handle)
                    {
                        handle.promise().scheduler_ = this;
                        // Use compile-time affinity if available
                        if constexpr (requires { decltype(task)::affinity; })
                        {
                            scheduleHandleWithAffinity(handle, decltype(task)::affinity);
                        }
                        else
                        {
                            scheduleHandle(handle);
                        }
                    }
                }
                co_return;
            },
            token
        );

        {
            std::lock_guard<std::mutex> lock(scheduledTasksMutex_);
            scheduledTasks_.push(scheduledTask);
        }

        return token;
    }

    // Schedule a task to execute repeatedly at intervals
    template <typename Callable>
    auto scheduleInterval(milliseconds interval, Callable&& callable) -> CancellationToken
    {
        CancellationToken token;

        // Store callable for repeated execution using std::function to handle lambdas with captures
        auto storedCallable = std::make_shared<std::function<Task<void>()>>(std::forward<Callable>(callable));

        // Create interval task using factory pattern to avoid recursive lambdas
        auto scheduledTask = std::make_shared<ScheduledTask>(
            ScheduledTask::Type::Interval,
            steady_clock::now() + interval,
            [storedCallable, this]() -> Task<void> {
                // Call the stored callable to get a Task
                auto task = (*storedCallable)();

                // Schedule the resulting task
                if constexpr (requires { task.handle_; })
                {
                    auto handle = task.handle_;
                    if (handle)
                    {
                        handle.promise().scheduler_ = this;
                        // Use compile-time affinity if available
                        if constexpr (requires { decltype(task)::affinity; })
                        {
                            scheduleHandleWithAffinity(handle, decltype(task)::affinity);
                        }
                        else
                        {
                            scheduleHandle(handle);
                        }
                    }
                }
                co_return;
            },
            token
        );

        scheduledTask->setInterval(interval);

        {
            std::lock_guard<std::mutex> lock(scheduledTasksMutex_);
            scheduledTasks_.push(scheduledTask);
        }

        return token;
    }

    // Reschedule an interval task after its child task completes
    void rescheduleIntervalTask(std::shared_ptr<ScheduledTask> intervalTask)
    {
        if (intervalTask->getType() == ScheduledTask::Type::Interval && !intervalTask->isCancelled())
        {
            auto completionTime = steady_clock::now();
            auto nextExecutionTime = completionTime + intervalTask->getInterval();

            // Create a new scheduled task for the next interval
            auto rescheduledTask = std::make_shared<ScheduledTask>(
                ScheduledTask::Type::Interval,
                nextExecutionTime,
                intervalTask->getTaskFactory(),
                intervalTask->getToken()
            );
            rescheduledTask->setInterval(intervalTask->getInterval());

            {
                std::lock_guard<std::mutex> lock(scheduledTasksMutex_);
                scheduledTasks_.push(rescheduledTask);
            }
        }
    }

    // Track active interval tasks to prevent overlapping executions
    void markIntervalTaskActive(std::shared_ptr<ScheduledTask> intervalTask)
    {
        std::lock_guard<std::mutex> lock(activeIntervalsMutex_);
        activeIntervals_.insert(intervalTask);
    }

    // Mark interval task as completed and reschedule if needed
    void markIntervalTaskCompleted(std::shared_ptr<ScheduledTask> intervalTask)
    {
        {
            std::lock_guard<std::mutex> lock(activeIntervalsMutex_);
            activeIntervals_.erase(intervalTask);
        }

        // Reschedule the interval task for next execution
        rescheduleIntervalTask(intervalTask);
    }

    // Check if an interval task is currently active
    bool isIntervalTaskActive(std::shared_ptr<ScheduledTask> intervalTask)
    {
        std::lock_guard<std::mutex> lock(activeIntervalsMutex_);
        return activeIntervals_.find(intervalTask) != activeIntervals_.end();
    }

private:
    // Helper method to schedule tasks with known affinity
    template <typename TaskType>
    void scheduleTask(TaskType&& task, ThreadAffinity affinity)
    {
        if constexpr (requires { task.handle_; })
        {
            auto handle = task.handle_;
            if (handle)
            {
                // Propagate scheduler reference to the task's promise
                handle.promise().scheduler_ = this;
                scheduleHandleWithAffinity(handle, affinity);
            }
        }
    }

    // Schedule handle with explicit affinity (compile-time optimization)
    void scheduleHandleWithAffinity(std::coroutine_handle<> handle, ThreadAffinity affinity)
    {
        if (affinity == ThreadAffinity::Main)
        {
            // Schedule on main thread
            std::lock_guard<std::mutex> lock(mainThreadTasksMutex_);
            mainThreadTasks_.push_back(handle);
        }
        else
        {
            // Schedule on worker thread
            workerPool_->enqueueToAnyWorker(handle);
        }
    }









    // Get the number of worker threads
    auto getWorkerThreadCount() const -> size_t
    {
        return workerPool_->getThreadCount();
    }

    // Check if the scheduler is running
    auto isRunning() const -> bool
    {
        return running_.load();
    }

private:
    // Worker pool for executing coroutines
    std::unique_ptr<WorkerPool> workerPool_;

    // Main thread task queue (legacy support)
    std::vector<std::coroutine_handle<>> mainThreadTasks_;
    std::mutex mainThreadTasksMutex_;

    // Scheduled tasks (delayed and interval tasks)
    std::queue<std::shared_ptr<ScheduledTask>> scheduledTasks_;
    std::mutex scheduledTasksMutex_;

    // Active interval tasks (to prevent overlapping executions)
    std::unordered_set<std::shared_ptr<ScheduledTask>> activeIntervals_;
    std::mutex activeIntervalsMutex_;

    // Timer thread for processing scheduled tasks
    std::thread timerThread_;
    std::atomic<bool> timerRunning_{true};

    // Atomic flag for running state
    std::atomic<bool> running_{true};

    // Thread ID for main thread
    std::thread::id mainThreadId_;

    // Helper method to determine thread affinity for coroutines
    ThreadAffinity determineThreadAffinity(std::coroutine_handle<> coroutine);

    // Timer thread function
    void timerThreadFunction();

    // Schedule a coroutine handle to be resumed at a later time as soon as a thread
    // is available. This is the core handle-based scheduling method.
    void scheduleHandle(std::coroutine_handle<> coroutine)
    {

        if (!coroutine || coroutine.done())
        {
            return;
        }

        // Fallback: default to main thread when compile-time affinity is not available
        ThreadAffinity affinity = ThreadAffinity::Main;

        if (affinity == ThreadAffinity::Main)
        {
            // Schedule on main thread
            std::lock_guard<std::mutex> lock(mainThreadTasksMutex_);
            mainThreadTasks_.push_back(coroutine);
        }
        else
        {
            // Schedule on worker thread
            workerPool_->enqueueToAnyWorker(coroutine);
        }
    }
};

//
// Scheduler implementation
//
inline Scheduler::Scheduler(size_t numThreads)
    : mainThreadId_(std::this_thread::get_id())
{
    // Create worker pool
    workerPool_ = std::make_unique<WorkerPool>(this, numThreads);

    // Start worker threads
    workerPool_->start();

    // Start timer thread for delayed/interval tasks
    timerThread_ = std::thread([this]() { timerThreadFunction(); });
}

inline Scheduler::~Scheduler() noexcept
{
    // Cleanup in destructor
    running_.store(false);

    // Stop timer thread
    timerRunning_.store(false);
    if (timerThread_.joinable())
    {
        timerThread_.join();
    }

    if (workerPool_)
    {
        workerPool_->stop();
    }
}





inline void Scheduler::timerThreadFunction()
{
    while (timerRunning_.load())
    {
        auto now = steady_clock::now();

        // Process expired scheduled tasks (same logic as runExpiredTasks)
        {
            std::lock_guard<std::mutex> lock(scheduledTasksMutex_);
            std::queue<std::shared_ptr<ScheduledTask>> remainingTasks;

            while (!scheduledTasks_.empty())
            {
                auto task = scheduledTasks_.front();
                scheduledTasks_.pop();

                if (task->shouldExecute(now))
                {
                    // Execute the task
                    auto nextTime = task->execute();

                    if (task->getType() == ScheduledTask::Type::Interval && nextTime != time_point::max())
                    {
                        // For interval tasks, reschedule with new execution time
                        auto rescheduledTask = std::make_shared<ScheduledTask>(
                            ScheduledTask::Type::Interval,
                            nextTime,
                            task->getTaskFactory(),
                            task->getToken()
                        );
                        rescheduledTask->setInterval(task->getInterval());
                        remainingTasks.push(rescheduledTask);
                    }
                    // One-time tasks are discarded after execution
                }
                else if (!task->isCancelled())
                {
                    // Keep non-expired, non-cancelled tasks
                    remainingTasks.push(task);
                }
                // Cancelled tasks are discarded
            }

            scheduledTasks_ = std::move(remainingTasks);
        }

        // Sleep for a short interval before checking again
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

inline ThreadAffinity Scheduler::determineThreadAffinity(std::coroutine_handle<> coroutine)
{
    if (!coroutine)
    {
        return ThreadAffinity::Main; // Default to main thread
    }

    // Try to determine affinity from the promise type at compile time
    // This is a fallback for cases where compile-time affinity is not available
    if (std::this_thread::get_id() != mainThreadId_)
    {
        return ThreadAffinity::Worker;
    }

    return ThreadAffinity::Main;
}

} // namespace CoroRoro
