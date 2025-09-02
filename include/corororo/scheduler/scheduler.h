#pragma once

#include <corororo/coroutine/types.h>
#include <corororo/scheduler/worker_pool.h>
#include <concurrentqueue/concurrentqueue.h>
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

namespace CoroRoro {

// Forward declarations for friend classes
namespace detail {
    struct InitialAwaiter;
}

// TransferPolicy will be implemented later once basic functionality works

using steady_clock = std::chrono::steady_clock;
using time_point = std::chrono::time_point<steady_clock>;
using milliseconds = std::chrono::milliseconds;

// Template wrapper that encodes queue affinity at compile time
template <ThreadAffinity Affinity>
class QueueWrapper {
private:
    moodycamel::ConcurrentQueue<std::coroutine_handle<>> queue_;

public:
    // No runtime thread checks needed - affinity is known at compile time!
    CORO_HOT CORO_INLINE std::coroutine_handle<> getNextTask() {
        std::coroutine_handle<> handle;
        if (queue_.try_dequeue(handle)) {
            return handle;
        }
        return std::noop_coroutine();
    }

    CORO_HOT CORO_INLINE void enqueueTask(std::coroutine_handle<> handle) {
        queue_.enqueue(handle);
    }

    CORO_HOT CORO_INLINE bool tryDequeue(std::coroutine_handle<>& handle) {
        return queue_.try_dequeue(handle);
    }
};

class Scheduler {
public:
    explicit Scheduler(size_t workerThreadCount = std::thread::hardware_concurrency() - 1) {
        mainThreadId_ = std::this_thread::get_id();
        workerPool_ = std::make_unique<WorkerPool>(this, workerThreadCount);
        workerQueues_.resize(workerThreadCount);
        running_.store(true);
    }

    ~Scheduler() {
        running_.store(false);
        // Worker pool will clean up its threads
    }

    // Disable copy and move
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    Scheduler(Scheduler&&) = delete;
    Scheduler& operator=(Scheduler&&) = delete;

    // Basic scheduling
    template <typename TaskType>
    void schedule(TaskType task);

    // Process expired tasks - minimal implementation
    auto runExpiredTasks(time_point referenceTime = steady_clock::now()) -> milliseconds;

    // Get worker thread count
    auto getWorkerThreadCount() const -> size_t { return workerPool_->getThreadCount(); }

    // Get main thread ID
    auto getMainThreadId() const -> std::thread::id { return mainThreadId_; }

    // TransferPolicy support methods (public for TransferPolicy access)
    template <ThreadAffinity Affinity>
    auto getNextTaskWithAffinity() -> std::coroutine_handle<>;

    // Friend declarations for awaiters that need access to private methods
    friend struct detail::InitialAwaiter;

private:
    // Templated methods for generic awaiters (private - accessed via friends)
    template <ThreadAffinity Affinity>
    auto scheduleTaskWithAffinity(std::coroutine_handle<> handle) -> std::coroutine_handle<>;

    template <ThreadAffinity Affinity>
    auto finalizeTaskWithAffinity(std::coroutine_handle<> handle) -> std::coroutine_handle<>;

    // TransferPolicy support methods
    template <ThreadAffinity Affinity>
    void scheduleHandleWithAffinity(std::coroutine_handle<> handle) noexcept;

    // Helper methods
    auto scheduleMainThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<>;
    auto scheduleWorkerThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<>;
    auto finalizeMainThreadTask([[maybe_unused]] std::coroutine_handle<> handle) -> std::coroutine_handle<>;
    auto finalizeWorkerThreadTask([[maybe_unused]] std::coroutine_handle<> handle) -> std::coroutine_handle<>;



    // Member variables
    std::unique_ptr<WorkerPool> workerPool_;
    std::thread::id mainThreadId_;
    std::atomic<bool> running_{true};

    // Lockless task queues using template dispatch
    QueueWrapper<ThreadAffinity::Main> mainQueue_;
    std::vector<QueueWrapper<ThreadAffinity::Worker>> workerQueues_;
};

//
// Template implementations
//

    template <typename TaskType>
void Scheduler::schedule(TaskType task) {
    // Extract handle and route to appropriate queue
    auto handle = task.handle_;
    
    if (handle && !handle.done()) {
        // Inject scheduler pointer into the promise for awaiter delegation
        auto& promise = handle.promise();
        promise.scheduler_ = this;
        
        // Route to appropriate queue based on task affinity
        if constexpr (TaskType::affinity == ThreadAffinity::Main) {
            scheduleMainThreadTask(handle);
        } else {
            scheduleWorkerThreadTask(handle);
        }
    }
}

inline auto Scheduler::runExpiredTasks(time_point referenceTime) -> milliseconds {
    auto start = steady_clock::now();

    // Use referenceTime parameter to avoid unused warning
    auto currentTime = referenceTime;

    // Process main thread tasks using lockless queue
    std::coroutine_handle<> handle;
    while (mainQueue_.tryDequeue(handle)) {
        if (handle && !handle.done()) {
            handle.resume();
        }
    }

    return std::chrono::duration_cast<milliseconds>(steady_clock::now() - start);
}



auto Scheduler::scheduleMainThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    mainQueue_.enqueueTask(handle);
    return std::noop_coroutine(); // Return noop for now - will be replaced with symmetric transfer
}

auto Scheduler::scheduleWorkerThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    workerPool_->enqueueToAnyWorker(handle);
    return std::noop_coroutine(); // Return noop for now - will be replaced with symmetric transfer
}

auto Scheduler::finalizeMainThreadTask([[maybe_unused]] std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    // Use lockless queue for symmetric transfer
    return mainQueue_.getNextTask();
}

auto Scheduler::finalizeWorkerThreadTask([[maybe_unused]] std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    // Delegate to worker pool for next task
    return workerPool_->dequeueFromAnyWorker();
}

// Template implementations for generic awaiters (private - accessed via friends)
template <ThreadAffinity Affinity>
auto Scheduler::scheduleTaskWithAffinity(std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    if constexpr (Affinity == ThreadAffinity::Main) {
        return scheduleMainThreadTask(handle);
    } else {
        return scheduleWorkerThreadTask(handle);
    }
}

template <ThreadAffinity Affinity>
auto Scheduler::finalizeTaskWithAffinity(std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    if constexpr (Affinity == ThreadAffinity::Main) {
        return finalizeMainThreadTask(handle);
    } else {
        return finalizeWorkerThreadTask(handle);
    }
}

template <ThreadAffinity Affinity>
void Scheduler::scheduleHandleWithAffinity(std::coroutine_handle<> handle) noexcept {
    if constexpr (Affinity == ThreadAffinity::Main) {
        // Direct lockless enqueue to main thread - zero syscalls!
        mainQueue_.enqueueTask(handle);
    } else {
        // For worker threads, we still need to select which worker queue
        // TODO: Implement round-robin or load-based selection
        scheduleWorkerThreadTask(handle);
    }
}

template <ThreadAffinity Affinity>
auto Scheduler::getNextTaskWithAffinity() -> std::coroutine_handle<> {
    if constexpr (Affinity == ThreadAffinity::Main) {
        // Get next task from main thread queue using lockless queue
        return mainQueue_.getNextTask();
    } else {
        // Get next task from worker pool
        return workerPool_->dequeueFromAnyWorker();
    }
}

// The TransferPolicy is a compile-time mechanism to determine how to transition
// from one coroutine to another based on their thread affinities. It is the
// core of the efficient scheduling logic.
template <ThreadAffinity CurrentAffinity, ThreadAffinity NextAffinity>
[[nodiscard]] CORO_HOT CORO_INLINE auto TransferPolicy<CurrentAffinity, NextAffinity>::transfer(Scheduler* scheduler, std::coroutine_handle<> handle) noexcept -> std::coroutine_handle<>
{
    if constexpr (CurrentAffinity == NextAffinity)
    {
        // OPTIMIZATION: The current and next tasks have the same thread affinity.
        // There's no need to go through the scheduler's queue. We can perform a
        // direct symmetric transfer by returning the handle to the next task,
        // which the runtime will immediately resume.
        return CORO_LIKELY(handle);
    }
    else
    {
        // AFFINITY CHANGE: The tasks have different affinities. We must suspend
        // the current execution flow and involve the scheduler.
        // 1. Schedule the new task on the queue corresponding to its affinity.
        scheduler->scheduleHandleWithAffinity<NextAffinity>(handle);

        // 2. Symmetrically transfer to the next available task on the *current*
        //    thread's queue. This keeps the current thread busy.
        return scheduler->template getNextTaskWithAffinity<CurrentAffinity>();
    }
}

} // namespace CoroRoro
