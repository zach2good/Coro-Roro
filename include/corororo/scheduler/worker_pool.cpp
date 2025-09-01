#include "worker_pool.hpp"
#include "scheduler.hpp"
#include <algorithm>
#include <stdexcept>

namespace CoroRoro
{

// WorkQueue implementation
void WorkQueue::enqueue(std::coroutine_handle<> handle)
{
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push(handle);
}

auto WorkQueue::tryDequeue(std::coroutine_handle<>& handle) -> bool
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

auto WorkQueue::empty() const -> bool
{
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
}

// WorkerThread implementation
WorkerThread::WorkerThread(Scheduler* scheduler, size_t threadId)
    : scheduler_(scheduler)
    , threadId_(threadId)
{
}

WorkerThread::~WorkerThread()
{
    stop();
}

void WorkerThread::start()
{
    if (!running_.load())
    {
        running_.store(true);
        thread_ = std::thread(&WorkerThread::workerLoop, this);
    }
}

void WorkerThread::stop()
{
    running_.store(false);
    
    if (thread_.joinable())
    {
        thread_.join();
    }
}

void WorkerThread::enqueueWork(std::coroutine_handle<> handle)
{
    workQueue_.enqueue(handle);
}

void WorkerThread::workerLoop()
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

// WorkerPool implementation
WorkerPool::WorkerPool(Scheduler* scheduler, size_t numThreads)
    : scheduler_(scheduler)
{
    // Create worker threads
    for (size_t i = 0; i < numThreads; ++i)
    {
        workers_.push_back(std::make_unique<WorkerThread>(scheduler, i));
    }
}

WorkerPool::~WorkerPool()
{
    stop();
}

void WorkerPool::start()
{
    for (auto& worker : workers_)
    {
        worker->start();
    }
}

void WorkerPool::stop()
{
    for (auto& worker : workers_)
    {
        worker->stop();
    }
}

void WorkerPool::enqueueToWorker(size_t workerId, std::coroutine_handle<> handle)
{
    if (workerId < workers_.size())
    {
        workers_[workerId]->enqueueWork(handle);
    }
}

void WorkerPool::enqueueToAnyWorker(std::coroutine_handle<> handle)
{
    // Round-robin distribution
    size_t worker = currentWorker_.fetch_add(1) % workers_.size();
    enqueueToWorker(worker, handle);
}

} // namespace CoroRoro
