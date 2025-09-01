#pragma once

#include <corororo/coroutine/task.h>
#include <corororo/coroutine/types.h>
#include <corororo/scheduler/worker_pool.h>
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
    template <typename DerivedPromise>
    struct FinalAwaiter;

    struct TaskInitialAwaiter;
    struct TaskFinalAwaiter;
}

using steady_clock = std::chrono::steady_clock;
using time_point = std::chrono::time_point<steady_clock>;
using milliseconds = std::chrono::milliseconds;

class Scheduler {
public:
    explicit Scheduler(size_t workerThreadCount = std::thread::hardware_concurrency() - 1) {
        mainThreadId_ = std::this_thread::get_id();
        workerPool_ = std::make_unique<WorkerPool>(this, workerThreadCount);
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

    // Friend declarations for awaiters that need access to private methods
    template <typename DerivedPromise>
    friend struct detail::FinalAwaiter;

    friend struct detail::TaskInitialAwaiter;
    friend struct detail::TaskFinalAwaiter;

private:
    // Templated methods for generic awaiters (private - accessed via friends)
    template <ThreadAffinity Affinity>
    auto scheduleTaskWithAffinity(std::coroutine_handle<> handle) -> std::coroutine_handle<>;

    template <ThreadAffinity Affinity>
    auto finalizeTaskWithAffinity(std::coroutine_handle<> handle) -> std::coroutine_handle<>;

    // Helper methods
    auto scheduleMainThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<>;
    auto scheduleWorkerThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<>;
    auto finalizeMainThreadTask([[maybe_unused]] std::coroutine_handle<> handle) -> std::coroutine_handle<>;
    auto finalizeWorkerThreadTask([[maybe_unused]] std::coroutine_handle<> handle) -> std::coroutine_handle<>;

    // Member variables
    std::unique_ptr<WorkerPool> workerPool_;
    std::thread::id mainThreadId_;
    std::atomic<bool> running_{true};

    // Simple task queues
    std::mutex mainThreadTasksMutex_;
    std::deque<std::coroutine_handle<>> mainThreadTasks_;
};

//
// Template implementations
//

template <typename TaskType>
void Scheduler::schedule(TaskType task) {
    // Extract handle and route to appropriate queue
    auto handle = task.handle_;
    if (handle && !handle.done()) {
        // For now, just put everything on main thread
        scheduleMainThreadTask(handle);
    }
}

inline auto Scheduler::runExpiredTasks(time_point referenceTime) -> milliseconds {
    auto start = steady_clock::now();

    // Use referenceTime parameter to avoid unused warning
    auto currentTime = referenceTime;

    // Process main thread tasks
    while (!mainThreadTasks_.empty()) {
        auto handle = mainThreadTasks_.back();
        mainThreadTasks_.pop_back();

        if (handle && !handle.done()) {
            handle.resume();
        }
    }

    return std::chrono::duration_cast<milliseconds>(steady_clock::now() - start);
}



inline auto Scheduler::scheduleMainThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    std::lock_guard<std::mutex> lock(mainThreadTasksMutex_);
    mainThreadTasks_.push_back(handle);
    return std::noop_coroutine(); // Return noop for now - will be replaced with symmetric transfer
}

inline auto Scheduler::scheduleWorkerThreadTask(std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    workerPool_->enqueueToAnyWorker(handle);
    return std::noop_coroutine(); // Return noop for now - will be replaced with symmetric transfer
}

inline auto Scheduler::finalizeMainThreadTask([[maybe_unused]] std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    // Try to get next task for symmetric transfer
    {
        std::lock_guard<std::mutex> lock(mainThreadTasksMutex_);
        if (!mainThreadTasks_.empty()) {
            auto nextHandle = mainThreadTasks_.back();
            mainThreadTasks_.pop_back();
            return nextHandle;
        }
    }
    return std::noop_coroutine();
}

inline auto Scheduler::finalizeWorkerThreadTask([[maybe_unused]] std::coroutine_handle<> handle) -> std::coroutine_handle<> {
    // Delegate to worker pool for next task
    return workerPool_->dequeueFromAnyWorker();
}

// Template implementations (private - accessed via friends)
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

} // namespace CoroRoro