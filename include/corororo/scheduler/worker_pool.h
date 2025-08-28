#pragma once

#include <corororo/scheduler/schedulable_task.h>
#include <corororo/util/macros.h>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace CoroRoro
{

// Forward declaration
class Scheduler;

//
// WorkerPool
//
//   Manages a pool of worker threads that execute tasks with WorkerThread affinity.
//
class WorkerPool final
{
public:
    explicit WorkerPool(size_t numThreads, Scheduler& scheduler);
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

    // Task queue for worker threads
    std::queue<std::unique_ptr<ISchedulableTask>> workerThreadQueue_;
    std::mutex                                    workerThreadMutex_;
    std::condition_variable                       workerTaskAvailable_;
};

//
// Inline implementations
//

inline WorkerPool::WorkerPool(size_t numThreads, Scheduler& scheduler)
: scheduler_(scheduler)
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

    // Wake all worker threads
    workerTaskAvailable_.notify_all();

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
    {
        std::lock_guard<std::mutex> lock(workerThreadMutex_);
        workerThreadQueue_.emplace(std::move(task));
    }
    workerTaskAvailable_.notify_one();
}

inline void WorkerPool::workerThreadLoop()
{
    while (!shuttingDown_.load())
    {
        std::unique_ptr<ISchedulableTask> task;

        // Try to get a task from worker queue
        {
            std::unique_lock<std::mutex> lock(workerThreadMutex_);
            workerTaskAvailable_.wait(lock, [this]
                                      { return shuttingDown_.load() || !workerThreadQueue_.empty(); });

            if (shuttingDown_.load())
            {
                break;
            }

            if (!workerThreadQueue_.empty())
            {
                task = std::move(workerThreadQueue_.front());
                workerThreadQueue_.pop();
            }
        }

        // Execute the task if we got one
        if (task)
        {
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
        }
    }
}

} // namespace CoroRoro
