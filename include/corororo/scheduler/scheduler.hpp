#pragma once

#include <corororo/coroutine/task.hpp>
#include <corororo/coroutine/types.hpp>
#include <corororo/scheduler/worker_pool.hpp>
#include <corororo/scheduler/scheduled_task.hpp>
#include <corororo/scheduler/cancellation_token.hpp>
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

    // Legacy schedule method for backward compatibility
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

} // namespace CoroRoro
