#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <thread>
#include <vector>

#include "cancellation_token.h"
#include "enums.h"
#include "forward_declarations.h"
#include "interval_task.h"
#include "macros.h"
#include "task_base.h"

#include <concurrentqueue/concurrentqueue.h>

namespace CoroRoro
{

//
// Scheduler
//
// High-performance coroutine scheduler with interval task factory and cancellation support.
// Uses priority queue-based timer system with single-execution guarantees.
//

class Scheduler final
{
public:
    //
    // Constructor & Destructor
    //

    explicit Scheduler(size_t workerThreadCount = 4)
    {
        running_.store(true);

        workerThreads_.reserve(workerThreadCount);
        for (size_t i = 0; i < workerThreadCount; ++i)
        {
            workerThreads_.emplace_back(
                [this]
                {
                    workerLoop();
                });
        }
    }

    ~Scheduler()
    {
        running_.store(false);
        workerCondition_.notify_all();

        for (auto& thread : workerThreads_)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }

    Scheduler(const Scheduler&)            = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&)                 = delete;
    Scheduler& operator=(Scheduler&&)      = delete;

    //
    // Basic Scheduling
    //

    void notifyTaskComplete()
    {
        inFlightTasks_.fetch_sub(1, std::memory_order_relaxed);
    }

    // Takes a temporary Task object (an r-value) and assumes ownership of its
    // coroutine handle. This is a "sink" function.
    template <ThreadAffinity Affinity, typename T>
    HOT_PATH void schedule(detail::TaskBase<Affinity, T>&& task)
    {
        const auto handle = task.handle_;
        if (handle && !handle.done())
            LIKELY
            {
                inFlightTasks_.fetch_add(1, std::memory_order_relaxed);
                handle.promise().scheduler_ = this;

                // The coroutine's lifetime is now managed by the scheduler. We must
                // release the handle from the task object that was passed in to
                // prevent it from being destroyed when it goes out of scope.
                task.handle_ = nullptr;

                scheduleHandleWithAffinity<Affinity>(handle);
            }
    }

    // Schedule a callable that returns a Task/AsyncTask
    // Supports lambdas, std::bind, function objects, etc.
    template <typename Callable>
    void schedule(Callable&& callable)
        requires std::is_invocable_v<Callable> &&
                 requires { std::invoke_result_t<Callable>{}; } &&
                 requires { std::invoke_result_t<Callable>{}.handle_; }
    {
        auto task = std::forward<Callable>(callable)();
        schedule(std::move(task));
    }

    // Schedule a callable that returns void (non-coroutine)
    // Automatically wraps the callable in a coroutine for execution
    template <typename Callable>
    void schedule(Callable&& callable)
        requires std::is_invocable_v<Callable> &&
                 std::is_void_v<std::invoke_result_t<Callable>>;

    //
    // Task Execution
    //

    HOT_PATH auto runExpiredTasks() -> std::chrono::milliseconds
    {
        auto start = std::chrono::steady_clock::now();

        // Process expired interval tasks
        processExpiredIntervalTasks();

        while (inFlightTasks_.load(std::memory_order_acquire) > 0)
        {
            if (auto task = getNextMainThreadTask(); task && !task.done())
            {
                task.resume();
            }
            else
            {
                // If the main thread runs out of work and we've still got tasks in flight,
                // we should help out by running worker tasks until everything is done, or
                // until we get more main thread tasks.
                if (auto workerTask = getNextWorkerThreadTask(); workerTask && !workerTask.done())
                {
                    workerTask.resume();
                }
                else
                {
                    // If both queues are empty, yield.
                    std::this_thread::yield();
                }
            }
        }

        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }

    auto runExpiredTasks(std::chrono::steady_clock::time_point /* referenceTime */) -> std::chrono::milliseconds
    {
        auto start = std::chrono::steady_clock::now();

        // Process expired interval tasks
        processExpiredIntervalTasks();

        while (inFlightTasks_.load(std::memory_order_acquire) > 0)
        {
            if (auto task = getNextMainThreadTask(); task && !task.done())
            {
                task.resume();
            }
            else
            {
                // If the main thread runs out of work and we've still got tasks in flight,
                // we should help out by running worker tasks until everything is done, or
                // until we get more main thread tasks.
                if (auto workerTask = getNextWorkerThreadTask(); workerTask && !workerTask.done())
                {
                    workerTask.resume();
                }
                else
                {
                    // If both queues are empty, yield.
                    std::this_thread::yield();
                }
            }
        }

        const auto end = std::chrono::steady_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    }

    //
    // Basic Task Scheduling API
    //

    // Schedule a task that executes immediately
    // Callable must return Task<void> (return values are discarded)
    // Supports lambdas, std::bind, function objects, etc.
    template <typename Callable>
    void schedule(Callable&& callable);

    //
    // Interval Task Factory API
    //

    // Schedule an interval task that executes immediately, then every interval
    // Callable must return Task<void> (return values are discarded)
    // Supports lambdas, std::bind, function objects, etc.
    template <typename Rep, typename Period, typename Callable>
    auto scheduleInterval(std::chrono::duration<Rep, Period> interval,
                          Callable&&                         callable) -> CancellationToken;

    // Schedule a delayed task that executes once after delay
    // Callable must return Task<void> (return values are discarded)
    // Supports lambdas, std::bind, function objects, etc.
    template <typename Rep, typename Period, typename Callable>
    auto scheduleDelayed(std::chrono::duration<Rep, Period> delay,
                         Callable&&                         callable) -> CancellationToken;

    //
    // Status and Information
    //

    auto isRunning() const -> bool
    {
        return running_.load();
    }

    auto getWorkerThreadCount() const -> size_t
    {
        return workerThreads_.size();
    }

    auto getInFlightTaskCount() const -> size_t
    {
        return inFlightTasks_.load();
    }

    //
    // Internal Scheduling Methods
    //

    template <ThreadAffinity Affinity>
    FORCE_INLINE HOT_PATH void scheduleHandleWithAffinity(std::coroutine_handle<> handle) noexcept
    {
        if constexpr (Affinity == ThreadAffinity::Main)
        {
            scheduleMainThreadTask(handle);
        }
        else
        {
            scheduleWorkerThreadTask(handle);
        }
    }

    template <ThreadAffinity Affinity>
    FORCE_INLINE HOT_PATH auto getNextTaskWithAffinity() noexcept -> std::coroutine_handle<>
    {
        if constexpr (Affinity == ThreadAffinity::Main)
        {
            return getNextMainThreadTask();
        }
        else
        {
            return getNextWorkerThreadTask();
        }
    }

    // Static method to run a coroutine inline to completion without requiring a scheduler.
    // This function only works with Task<void> (Main affinity) and blocks until completion.
    // AsyncTask types are not supported to keep the implementation simple.
    static void runCoroutineInlineDetached(detail::TaskBase<ThreadAffinity::Main, void>&& task)
    {
        auto handle = task.handle();
        task.handle_ = nullptr;

        if (handle && !handle.done())
        {
            handle.resume();
        }

        if (handle)
        {
            handle.destroy();
        }
    }

private:
    void workerLoop()
    {
        while (running_.load())
        {
            std::coroutine_handle<> task = nullptr;

            // Try to get a task
            if (workerThreadTasks_.try_dequeue(task))
            {
                task.resume();
            }
            else
            {
                // Spin for 5ms before going to sleep to avoid CV overhead for quick tasks
                auto       spinStart    = std::chrono::steady_clock::now();
                const auto spinDuration = std::chrono::milliseconds(5);

                while (running_.load() && std::chrono::steady_clock::now() - spinStart < spinDuration)
                {
                    if (workerThreadTasks_.try_dequeue(task))
                    {
                        task.resume();
                        break; // Exit spin loop and continue main loop
                    }
                    // Aggressive spinning - no yield to maximize performance
                    // The 5ms limit prevents excessive CPU usage
                }

                // If still no task after spinning, go to sleep
                if (!task)
                {
                    std::unique_lock<std::mutex> lock(workerMutex_);
                    workerCondition_.wait(
                        lock,
                        [this]
                        {
                            return !running_.load() || workerThreadTasks_.size_approx() > 0;
                        });
                }
            }
        }
    }

    FORCE_INLINE void scheduleMainThreadTask(std::coroutine_handle<> handle) noexcept
    {
        if (handle && !handle.done())
        {
            mainThreadTasks_.enqueue(handle);
        }
    }

    void scheduleWorkerThreadTask(std::coroutine_handle<> handle) noexcept
    {
        if (handle && !handle.done())
        {
            workerThreadTasks_.enqueue(handle);
            workerCondition_.notify_one();
        }
    }

    FORCE_INLINE auto getNextMainThreadTask() noexcept -> std::coroutine_handle<>
    {
        std::coroutine_handle<> handle = nullptr;
        if (mainThreadTasks_.try_dequeue(handle))
        {
            return handle;
        }
        return std::noop_coroutine();
    }

    auto getNextWorkerThreadTask() noexcept -> std::coroutine_handle<>
    {
        std::coroutine_handle<> handle = nullptr;
        if (workerThreadTasks_.try_dequeue(handle))
        {
            return handle;
        }
        return std::noop_coroutine();
    }

    // Process expired interval tasks
    void processExpiredIntervalTasks();

    // Task queues
    moodycamel::ConcurrentQueue<std::coroutine_handle<>> mainThreadTasks_;
    moodycamel::ConcurrentQueue<std::coroutine_handle<>> workerThreadTasks_;

    // Worker thread management
    std::vector<std::thread> workerThreads_;
    std::mutex               workerMutex_;
    std::condition_variable  workerCondition_;

    // Timer system
    std::priority_queue<std::unique_ptr<IntervalTask>> intervalQueue_;
    std::mutex                                         timerMutex_;

    // State
    std::atomic<bool>   running_{ true };
    std::atomic<size_t> inFlightTasks_{ 0 };
};

//
// Scheduler Method Implementations
//

// Implementation of processExpiredIntervalTasks moved here to avoid incomplete type issues
inline void Scheduler::processExpiredIntervalTasks()
{
    auto now = std::chrono::steady_clock::now();

    {
        std::lock_guard<std::mutex> lock(timerMutex_);

        while (!intervalQueue_.empty())
        {
            auto& intervalTask = intervalQueue_.top();

            if (intervalTask->getNextExecution() > now)
            {
                break; // No more expired tasks
            }

            // Remove from priority queue temporarily
            auto task = std::move(const_cast<std::unique_ptr<IntervalTask>&>(intervalQueue_.top()));
            intervalQueue_.pop();

            // Execute the task
            task->execute();

            // If it's not a one-time task and not cancelled, reschedule it
            if (!task->isCancelled() && !task->isOneTime())
            {
                intervalQueue_.push(std::move(task));
            }
        }
    }
}

template <typename Callable>
void Scheduler::schedule(Callable&& callable)
{
    // Create the task from the callable
    auto task = callable();

    // Schedule the task for execution
    scheduleHandleWithAffinity<ThreadAffinity::Main>(task.handle());
}

template <typename Rep, typename Period, typename Callable>
auto Scheduler::scheduleInterval(std::chrono::duration<Rep, Period> interval,
                                 Callable&&                         callable) -> CancellationToken
{
    // Convert to milliseconds for consistency
    auto intervalMs = std::chrono::duration_cast<std::chrono::milliseconds>(interval);

    // Create the factory function
    auto factory = [callable = std::forward<Callable>(callable)]() -> CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void>
    {
        return callable();
    };

    // Create the interval task
    auto intervalTask = std::make_unique<IntervalTask>(
        this,
        std::move(factory),
        intervalMs,
        false // Not a one-time task
    );

    // Create the cancellation token
    CancellationToken token(intervalTask.get(), this);

    // Add to priority queue
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        intervalQueue_.push(std::move(intervalTask));
    }

    return token;
}

template <typename Rep, typename Period, typename Callable>
auto Scheduler::scheduleDelayed(std::chrono::duration<Rep, Period> delay,
                                Callable&&                         callable) -> CancellationToken
{
    // Convert to milliseconds for consistency
    auto delayMs = std::chrono::duration_cast<std::chrono::milliseconds>(delay);

    // Create the factory function
    auto factory = [callable = std::forward<Callable>(callable)]() -> CoroRoro::detail::TaskBase<CoroRoro::ThreadAffinity::Main, void>
    {
        return callable();
    };

    // Create the interval task (one-time task)
    auto intervalTask = std::make_unique<IntervalTask>(
        this,
        std::move(factory),
        delayMs,
        true // One-time task
    );

    // Set the next execution time to now + delay
    intervalTask->setNextExecution(std::chrono::steady_clock::now() + delayMs);

    // Create the cancellation token
    CancellationToken token(intervalTask.get(), this);

    // Add to priority queue
    {
        std::lock_guard<std::mutex> lock(timerMutex_);
        intervalQueue_.push(std::move(intervalTask));
    }

    return token;
}

} // namespace CoroRoro
