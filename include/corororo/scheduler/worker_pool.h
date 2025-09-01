#pragma once

#include <corororo/coroutine/types.h>
#include <atomic>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace CoroRoro
{

// Forward declarations
class Scheduler;

//
// WorkQueue
//
//   Thread-safe queue for storing coroutine handles.
//   Uses a simple mutex-protected queue for now.
//
class WorkQueue final
{
public:
    // Enqueue a coroutine handle
    void enqueue(std::coroutine_handle<> handle);

    // Try to dequeue a coroutine handle
    auto tryDequeue(std::coroutine_handle<>& handle) -> bool;

    // Check if the queue is empty
    auto empty() const -> bool;

private:
    mutable std::mutex mutex_;
    std::queue<std::coroutine_handle<>> queue_;
};

//
// WorkerThread
//
//   A worker thread that continuously processes coroutines from its work queue.
//
class WorkerThread final
{
public:
    explicit WorkerThread(Scheduler* scheduler, size_t threadId);
    ~WorkerThread();

    // Start the worker thread
    void start();

    // Stop the worker thread
    void stop();

    // Enqueue work to this worker's queue
    void enqueueWork(std::coroutine_handle<> handle);

private:
    // Main worker loop
    void workerLoop();

    // Reference to the scheduler
    Scheduler* scheduler_;

    // Thread ID for this worker
    size_t threadId_;

    // The actual worker thread
    std::thread thread_;

    // Work queue for this worker
    WorkQueue workQueue_;

    // Atomic flag for running state
    std::atomic<bool> running_{false};
};

//
// WorkerPool
//
//   Manages a pool of worker threads and distributes work among them.
//
class WorkerPool final
{
public:
    explicit WorkerPool(Scheduler* scheduler, size_t numThreads);
    ~WorkerPool();

    // Start all worker threads
    void start();

    // Stop all worker threads
    void stop();

    // Enqueue work to a specific worker
    void enqueueToWorker(size_t workerId, std::coroutine_handle<> handle);

    // Enqueue work to any available worker (round-robin distribution)
    void enqueueToAnyWorker(std::coroutine_handle<> handle);

    // Get the number of worker threads
    auto getThreadCount() const -> size_t
    {
        return workers_.size();
    }

private:
    // Reference to the scheduler
    Scheduler* scheduler_;

    // Vector of worker threads
    std::vector<std::unique_ptr<WorkerThread>> workers_;

    // Current worker index for round-robin distribution
    std::atomic<size_t> currentWorker_{0};
};

//
// WorkQueue implementation
//
inline void WorkQueue::enqueue(std::coroutine_handle<> handle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(handle);
}

inline auto WorkQueue::tryDequeue(std::coroutine_handle<>& handle) -> bool
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_.empty())
    {
        return false;
    }

    handle = queue_.front();
    queue_.pop();
    return true;
}

inline auto WorkQueue::empty() const -> bool
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

//
// WorkerThread implementation
//
inline WorkerThread::WorkerThread(Scheduler* scheduler, size_t threadId)
    : scheduler_(scheduler)
    , threadId_(threadId)
{
}

inline WorkerThread::~WorkerThread()
{
    stop();
}

inline void WorkerThread::start()
{
    if (!running_.load())
    {
        running_.store(true);
        thread_ = std::thread(&WorkerThread::workerLoop, this);
    }
}

inline void WorkerThread::stop()
{
    running_.store(false);

    if (thread_.joinable())
    {
        thread_.join();
    }
}

inline void WorkerThread::enqueueWork(std::coroutine_handle<> handle)
{
    workQueue_.enqueue(handle);
}

inline void WorkerThread::workerLoop()
{
    while (running_.load())
    {
        std::coroutine_handle<> handle;

        if (workQueue_.tryDequeue(handle))
        {
            if (handle && !handle.done())
            {
                // Resume the coroutine
                handle.resume();
            }
        }
        else
        {
            // No work available, yield to other threads
            std::this_thread::yield();
        }
    }
}

//
// WorkerPool implementation
//
inline WorkerPool::WorkerPool(Scheduler* scheduler, size_t numThreads)
    : scheduler_(scheduler)
{
    // Create worker threads
    for (size_t i = 0; i < numThreads; ++i)
    {
        workers_.push_back(std::make_unique<WorkerThread>(scheduler, i));
    }
}

inline WorkerPool::~WorkerPool()
{
    stop();
}

inline void WorkerPool::start()
{
    for (auto& worker : workers_)
    {
        worker->start();
    }
}

inline void WorkerPool::stop()
{
    for (auto& worker : workers_)
    {
        worker->stop();
    }
}

inline void WorkerPool::enqueueToWorker(size_t workerId, std::coroutine_handle<> handle)
{
    if (workerId < workers_.size())
    {
        workers_[workerId]->enqueueWork(handle);
    }
}

inline void WorkerPool::enqueueToAnyWorker(std::coroutine_handle<> handle)
{
    // Round-robin distribution
    size_t worker = currentWorker_.fetch_add(1) % workers_.size();
    enqueueToWorker(worker, handle);
}

} // namespace CoroRoro
