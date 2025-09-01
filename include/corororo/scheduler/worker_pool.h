#pragma once

#include <corororo/scheduler/schedulable_task.h>
#include <corororo/util/macros.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include <concurrentqueue/concurrentqueue.h>

namespace CoroRoro
{

// Forward declaration
class Scheduler;

//
// WorkerPool
//
//   Manages a pool of worker threads that execute tasks with WorkerThread affinity.
//
//   LOCK-FREE IMPLEMENTATION:
//   - Uses moodycamel::ConcurrentQueue for lock-free task management
//   - Eliminates mutex contention in worker pool
//   - This was the remaining bottleneck in thread switching overhead
//
class WorkerPool final
{
public:
    explicit WorkerPool(size_t numThreads, Scheduler& scheduler, 
                       std::chrono::milliseconds spinDuration = std::chrono::milliseconds(5));
    ~WorkerPool();

    NO_MOVE_NO_COPY(WorkerPool);

    //
    // Task management
    //

    void enqueueTask(std::unique_ptr<ISchedulableTask> task);
    void shutdown();

private:
    //
    // Worker thread management
    //

    void workerThreadLoop();
    void routeTaskToScheduler(std::unique_ptr<ISchedulableTask> task);

    Scheduler&               scheduler_;
    std::atomic<bool>        shuttingDown_{ false };
    std::vector<std::thread> workerThreads_;
    std::chrono::milliseconds spinDuration_;

    // LOCK-FREE TASK QUEUE: Replace std::queue + mutex with moodycamel::ConcurrentQueue
    // This eliminates the mutex contention bottleneck in worker pool
    moodycamel::ConcurrentQueue<std::unique_ptr<ISchedulableTask>> workerThreadQueue_;
    
    // Note: workerThreadMutex_ removed - no longer needed for lock-free queue
    // Note: workerTaskAvailable_ removed - no longer needed for lock-free queue
    // The lock-free queue handles all synchronization internally
};

//
// Inline implementations
//

inline WorkerPool::WorkerPool(size_t numThreads, Scheduler& scheduler, std::chrono::milliseconds spinDuration)
: scheduler_(scheduler), spinDuration_(spinDuration)
{
    // Start worker threads
    for (size_t i = 0; i < numThreads; ++i)
    {
        workerThreads_.emplace_back(
            [this]
            {
                workerThreadLoop();
            });
    }
}

inline WorkerPool::~WorkerPool()
{
    shutdown();
}

inline void WorkerPool::shutdown()
{
    // Set shutdown flag
    shuttingDown_.store(true);

    // Note: No need to notify threads - they will check shuttingDown_ flag
    // The lock-free queue doesn't require condition variable notifications

    // Join all worker threads
    for (auto& thread : workerThreads_)
    {
        if (thread.joinable())
        {
            thread.join();
        }
    }
}

inline void WorkerPool::enqueueTask(std::unique_ptr<ISchedulableTask> task)
{
    // LOCK-FREE ENQUEUE: No mutex lock/unlock needed
    // moodycamel::ConcurrentQueue handles all synchronization internally
    workerThreadQueue_.enqueue(std::move(task));
    
    // Note: No condition variable notification needed
    // Worker threads will poll the queue and find the task
}

inline void WorkerPool::workerThreadLoop()
{
    while (!shuttingDown_.load())
    {
        std::unique_ptr<ISchedulableTask> task;

        // LOCK-FREE DEQUEUE: Try to get a task from worker queue
        // No mutex lock/unlock or condition variable wait needed
        if (workerThreadQueue_.try_dequeue(task))
        {
            // Execute the task if we got one
            try
            {
                TaskState state = task->resume();

                if (state == TaskState::Suspended)
                {
                    // Task suspended - route back to scheduler
                    // Implementation moved to scheduler.h to avoid incomplete type issues
                    routeTaskToScheduler(std::move(task));
                }
                // If completed or failed, task is destroyed automatically
            }
            catch (...)
            {
                // TODO: Add proper error handling
            }

            // WORKER SPINNING: After task completion, spin for a short time
            // This reduces context switching when tasks arrive in bursts
            auto spinStart = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - spinStart < spinDuration_)
            {
                // Try to get another task while spinning
                if (workerThreadQueue_.try_dequeue(task))
                {
                    // Execute the task if we got one
                    try
                    {
                        TaskState state = task->resume();

                        if (state == TaskState::Suspended)
                        {
                            // Task suspended - route back to scheduler
                            routeTaskToScheduler(std::move(task));
                        }
                        // If completed or failed, task is destroyed automatically
                    }
                    catch (...)
                    {
                        // TODO: Add proper error handling
                    }

                    // Reset spin timer - we found work, so keep spinning
                    spinStart = std::chrono::steady_clock::now();
                }
                else
                {
                    // No task available - yield briefly while spinning
                    std::this_thread::yield();
                }
            }
        }
        else
        {
            // No task available - yield to other threads
            // This replaces the condition variable wait
            std::this_thread::yield();
        }
    }
}

} // namespace CoroRoro
