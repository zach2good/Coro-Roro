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

    // Schedule a coroutine handle to be resumed at a later time as soon as a thread
    // is available. This is the core handle-based scheduling method.
    void scheduleHandle(std::coroutine_handle<> coroutine);

    // Schedule method for Task types (main thread)
    template <typename T>
    void schedule(Task<T>&& task)
    {
        scheduleTask(std::move(task), ThreadAffinity::Main);
    }

    // Schedule method for AsyncTask types (worker threads)
    template <typename T>
    void schedule(AsyncTask<T>&& task)
    {
        scheduleTask(std::move(task), ThreadAffinity::Worker);
    }

    // Generic schedule method for any task type
    template <typename TaskType>
    void schedule(TaskType&& task)
    {
        // Extract the coroutine handle and schedule it
        auto handle = task.getHandle();
        if (handle)
        {
            // Propagate scheduler reference to the task's promise
            handle.promise().scheduler_ = this;
            scheduleHandle(handle);
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

private:
    // Helper method to schedule tasks with known affinity
    template <typename TaskType>
    void scheduleTask(TaskType&& task, ThreadAffinity affinity)
    {
        auto handle = task.getHandle();
        if (handle)
        {
            // Propagate scheduler reference to the task's promise
            handle.promise().scheduler_ = this;
            scheduleHandleWithAffinity(handle, affinity);
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
                if constexpr (requires { task.getHandle(); })
                {
                    auto handle = task.getHandle();
                    if (handle)
                    {
                        handle.promise().scheduler_ = this;
                        scheduleHandle(handle);
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
                if constexpr (requires { task.getHandle(); })
                {
                    auto handle = task.getHandle();
                    if (handle)
                    {
                        handle.promise().scheduler_ = this;
                        scheduleHandle(handle);
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



    // Run expired tasks on the main thread (legacy support)
    // Returns the time spent processing tasks
    auto runExpiredTasks(time_point referenceTime = steady_clock::now()) -> milliseconds;

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

inline void Scheduler::scheduleHandle(std::coroutine_handle<> coroutine)
{
    if (!coroutine || coroutine.done())
    {
        return;
    }

    // Determine thread affinity by checking the promise type
    ThreadAffinity affinity = determineThreadAffinity(coroutine);

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

    // Use heuristic based on current thread context
    // This is used for generic coroutine handles where we don't know the type at compile time
    if (std::this_thread::get_id() != mainThreadId_)
    {
        return ThreadAffinity::Worker;
    }

    return ThreadAffinity::Main;
}

} // namespace CoroRoro
