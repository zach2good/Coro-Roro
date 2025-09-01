#pragma once

#include <corororo/coroutine/types.hpp>
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

} // namespace CoroRoro
