#include "scheduler.hpp"
#include "worker_pool.hpp"
#include <algorithm>
#include <stdexcept>

namespace CoroRoro
{

Scheduler::Scheduler(size_t numThreads)
    : mainThreadId_(std::this_thread::get_id())
{
    // Create worker pool
    workerPool_ = std::make_unique<WorkerPool>(this, numThreads);

    // Start worker threads
    workerPool_->start();
}

Scheduler::~Scheduler() noexcept
{
    // Cleanup in destructor
    running_.store(false);

    if (workerPool_)
    {
        workerPool_->stop();
    }
}

void Scheduler::scheduleHandle(std::coroutine_handle<> coroutine)
{
    if (!coroutine || coroutine.done())
    {
        return;
    }

    // Always schedule on worker threads for now
    // The main thread queue is meant for immediate execution tasks, not suspended coroutines
    workerPool_->enqueueToAnyWorker(coroutine);
}

auto Scheduler::runExpiredTasks(time_point referenceTime) -> milliseconds
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

    return std::chrono::duration_cast<milliseconds>(steady_clock::now() - start);
}

} // namespace CoroRoro
